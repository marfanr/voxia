// #include "ehci.h"
//
// // #include "queue_head.h"
// #include <firmw/pci/pci.h>
// #include <firmw/usb/usb.h>
// #include <firmw/usb/usb_classcode.h>
// #include <firmw/usb/usb_hid.h>
// #include <libk/serial.h>
// #include <libk/str/memset.h>
// #include <libk/timer.h>
// #include <libk/type.h>
// #include <memory/phys_base_allocator.h>
//
// pci_device_t ehci_dev;
// EhciQH *qh_pool;
// EhciQTD *qtd_pool;
// ehci_operation_t *op;
// uint8_t addr = 1;
// uint32_t *framelist;
//
// usb_device *devices;
// uint8_t devices_index = 0;
// EhciQTD *inttd = 0;
// EhciQH *intqh = 0;
// uint8_t *itintr = 0;
//
// /**
//  * @brief Fungsi ini digunakan untuk mengalokasikan objek EhciQTD dari
//  pool
//  * yang tersedia. Jika ada EhciQTD yang tidak digunakan, maka akan
//  diambil dan
//  * ditandai sebagai digunakan. Jika tidak ada EhciQTD yang tersedia, maka
//  akan
//  * mengirimkan pesan error melalui serial.
//  *
//  * @return Pointer ke EhciQTD yang dialokasikan, atau 0 jika tidak ada
//  EhciQTD
//  * yang tersedia.
//  */
// static EhciQTD *alloc_td() {
//   EhciQTD *end = qtd_pool + MAX_QUE_TD;
//   for (EhciQTD *td = qtd_pool; td != end; ++td) {
//     if (!td->used) {
//       td->used = 1;
//       return td;
//     }
//   }
//   serial_send_string("gagal mengalokasikan QTD\n");
//   return 0;
// }
//
// /**
//  * @brief Fungsi ini digunakan untuk mengalokasikan QH (Queue Head) pada
//  EHCI.
//  * QH yang dialokasikan akan diambil dari pool QH yang tersedia.
//  *
//  * @return Pointer ke QH yang dialokasikan jika berhasil, NULL jika
//  gagal.
//  */
// static EhciQH *alloc_qh() {
//   EhciQH *end = qh_pool + MAX_QUE_HEAD;
//   for (EhciQH *qh = qh_pool; qh != end; ++qh) {
//     if (!qh->used) {
//       qh->used = 1;
//       return qh;
//     }
//   }
//   serial_send_string("alloc QH Failed\n");
//   return NULL;
// }
//
// static void ehci_port_reset(int port, ehci_operation_t *op) {
//   uint32_t status = (uint32_t)op->portsc[port];
//   status |= PORT_RESET;
//   op->portsc[port] = status;
//   serial_trace("resetting port %d\n", port);
//   usleep(100);
//   serial_trace("port %d reset done\n", port);
//   status &= ~PORT_RESET;
//   op->portsc[port] = status;
// }
//
// static void usb_set_addr(uint8_t addr) {
//   usb_setup_packet_t *cmd = (usb_setup_packet_t *)phys_base_alloc(1);
//   memset(cmd, 0, sizeof(usb_setup_packet_t));
//   cmd->bRequest = 0x05;
//   cmd->bmRequestType |= 0;      // direction is out
//   cmd->bmRequestType |= 0 << 5; // type is standard
//   cmd->bmRequestType |= 0;      // recieve
//   cmd->wValue = addr;
//   cmd->wIndex = 0;
//   cmd->wLength = 0;
//
//   EhciQTD *setup = (EhciQTD *)phys_base_alloc(1);
//   memset(setup, 0, sizeof(EhciQTD));
//
//   EhciQTD *status = (EhciQTD *)phys_base_alloc(1);
//   memset(status, 0, sizeof(EhciQTD));
//
//   setup->link = (uint32_t)status;
//   setup->altlink = 1;
//   setup->token |= (8 << 16);   // setup size
//   setup->token |= (1 << 7);    // actief
//   setup->token |= (0x2 << 8);  // type is setup
//   setup->token |= (0x3 << 10); // maxerror
//   setup->buffer[0] = (uint32_t)cmd;
//
//   status->link = 1;
//   status->altlink = 1;
//   status->token |= (1 << 8);    // PID instellen
//   status->token |= (1 << 31);   // toggle
//   status->token |= (1 << 7);    // actief
//   status->token |= (0x3 << 10); // maxerror
//
//   EhciQH *head = (EhciQH *)phys_base_alloc(1);
//   memset(head, 0, sizeof(EhciQH));
//
//   EhciQH *head2 = (EhciQH *)phys_base_alloc(1);
//   memset(head2, 0, sizeof(EhciQH));
//
//   head2->altTD = 1;
//   head2->nextTD = (uint32_t)setup; // qdts2
//   head2->qhlp = ((uint32_t)head) | 2;
//   head2->currentTD = 0;  // qdts1
//   head2->ch |= 1 << 14;  // dtc
//   head2->ch |= 64 << 16; // mplen
//   head2->ch |= 2 << 12;  // eps
//   head2->cap = 0x40000000;
//
//   head->altTD = 1;
//   head->nextTD = 1;
//   head->qhlp = ((uint32_t)head2) | 2;
//   head->currentTD = 0;
//   head->ch = 1 << 15; // T
//   head->token = 0x40;
//
//   op->asynclistaddr = (uint32_t)(uintptr_t)head;
//   op->usbcmd |= (1 << 5);
//
//   // debug_qh(qh2);
//
//   usleep(100);
//   serial_send_string("proccessing....\n");
//   boolean_t stall = 0;
//   for (int i = 0; i < 10; i++) {
//     usleep(100);
//
//     if (status->token & (1 << 6)) {
//       serial_send_string("halted\n");
//       head->done = 1;
//
//       uint32_t token = status->token;
//       serial_send_string("qh token : ");
//       serial_send_number(token, 2);
//       serial_send_string("\n");
//       break;
//     }
//
//     if (status->token & (1 << 5)) {
//       serial_send_string("Data Buffer Error\n");
//       head->done = 1;
//       break;
//     }
//
//     if (status->token & (1 << 4)) {
//       serial_send_string("Babble detected\n");
//       head->done = 1;
//       break;
//     }
//
//     if (status->token & (1 << 3)) {
//       serial_send_string("Transaction error\n");
//       head->done = 1;
//       break;
//     }
//   }
//
//   op->usbcmd &= ~(1 << 5);
// }
//
// static void *usb_get_descriptor(uint8_t addr, uint8_t type, uint8_t
// index,
//                                 uint8_t len) {
//   usb_setup_packet_t *cmd = (usb_setup_packet_t *)phys_base_alloc(1);
//   memset(cmd, 0, sizeof(usb_setup_packet_t));
//   cmd->bRequest = 0x06;
//   cmd->bmRequestType |= 0x80; // recieve
//   cmd->wValue = (type << 8) | index;
//   cmd->wIndex = 0;
//   cmd->wLength = len;
//
//   EhciQTD *setup = (EhciQTD *)phys_base_alloc(1);
//   memset(setup, 0, sizeof(EhciQTD));
//
//   EhciQTD *data = (EhciQTD *)phys_base_alloc(1);
//   memset(data, 0, sizeof(EhciQTD));
//
//   EhciQTD *status = (EhciQTD *)phys_base_alloc(1);
//   memset(status, 0, sizeof(EhciQTD));
//
//   uint8_t *it = (uint8_t *)phys_base_alloc(1);
//
//   setup->link = (uint32_t)data;
//   setup->altlink = 1;
//   setup->token |= (8 << 16);   // setup size
//   setup->token |= (1 << 7);    // actief
//   setup->token |= (0x2 << 8);  // type is setup
//   setup->token |= (0x3 << 10); // maxerror
//   setup->buffer[0] = (uint32_t)cmd;
//
//   data->link = (uint32_t)status;
//   data->altlink = 1;
//   data->token |= (len << 16); // setup size
//   data->token |= (1 << 7);    // aktif
//   data->token |= (1 << 31);   // toggle
//   data->token |= (0x1 << 8);  // type is in
//   data->token |= (0x3 << 10); // maxerror
//   data->buffer[0] = (uint32_t)it;
//
//   status->link = 1;
//   status->altlink = 1;
//   status->token |= (0 << 8);    // PID instellen
//   status->token |= (1 << 31);   // toggle
//   status->token |= (1 << 7);    // actief
//   status->token |= (0x3 << 10); // maxerror
//
//   EhciQH *head = (EhciQH *)phys_base_alloc(1);
//   memset(head, 0, sizeof(EhciQH));
//
//   EhciQH *head2 = (EhciQH *)phys_base_alloc(1);
//   memset(head2, 0, sizeof(EhciQH));
//
//   head2->altTD = 1;
//   head2->nextTD = (uint32_t)setup; // qdts2
//   head2->qhlp = ((uint32_t)head) | 2;
//   head2->currentTD = 0;  // qdts1
//   head2->ch |= 1 << 14;  // dtc
//   head2->ch |= 64 << 16; // mplen
//   head2->ch |= 2 << 12;  // eps
//   head2->ch |= addr;     // eps
//   head2->cap = 0x40000000;
//
//   //
//   // Eerste commando
//   head->altTD = 1;
//   head->nextTD = 1;
//   head->qhlp = ((uint32_t)head2) | 2;
//   head->currentTD = 0;
//   head->ch = 1 << 15; // T
//   head->token = 0x40;
//
//   op->asynclistaddr = (uint32_t)(uintptr_t)head;
//   op->usbcmd |= (1 << 5);
//
//   // debug_qh(qh2);
//
//   usleep(100);
//   serial_send_string("proccessing....\n");
//   boolean_t stall = 0;
//   for (int i = 0; i < 10; i++) {
//     usleep(100);
//     uint32_t token = status->token;
//     ;
//
//     if (status->token & (1 << 6)) {
//       serial_send_string("halted\n");
//       head->done = 1;
//       serial_send_string("qh token : ");
//       serial_send_number(token, 2);
//       serial_send_string("\n");
//       break;
//     }
//
//     if (status->token & (1 << 5)) {
//       serial_send_string("Data Buffer Error\n");
//       head->done = 1;
//       break;
//     }
//
//     if (status->token & (1 << 4)) {
//       serial_send_string("Babble detected\n");
//       head->done = 1;
//       break;
//     }
//
//     if (status->token & (1 << 3)) {
//       serial_send_string("Transaction error\n");
//       head->done = 1;
//       break;
//     }
//   }
//
//   op->usbcmd &= ~(1 << 5);
//
//   return (void *)it;
// }
//
// void ehci_send_packet_interupt(uint8_t addr, uint8_t length, void *data,
//                                uint8_t interrupt) {
//   inttd = (EhciQTD *)phys_base_alloc(1);
//   memset(inttd, 0, sizeof(EhciQTD));
//   EhciQTD *status = (EhciQTD *)phys_base_alloc(1);
//   memset(status, 0, sizeof(EhciQTD));
//
//   inttd->altlink = 1;             // end link
//   inttd->link = 1;                // end link
//   inttd->token |= (length << 16); // buffer length
//   inttd->token |= (1 << 7);       // active toggle
//   inttd->token |= (1 << 8);       // type is Transaction Descriptor
//   inttd->token |= (0x3 << 10);    // max 3x retry on error
//   inttd->token |= (0 << 31);      // data toggle
//   inttd->token |= (1 << 15);      // Interrupt On Complete
//   inttd->buffer[0] = (uint32_t)data;
//
//   intqh = (EhciQH *)phys_base_alloc(1);
//   EhciQH *qh2 = (EhciQH *)phys_base_alloc(1);
//   memset(intqh, 0, sizeof(EhciQH));
//   memset(qh2, 0, sizeof(EhciQH));
//
//   qh2->altTD = 1;
//   qh2->qhlp = ((uint32_t)intqh) | 2;
//   qh2->nextTD = (uint32_t)inttd;
//   qh2->currentTD = 0;
//   qh2->ch |= 1 << 14;   // dtc
//   qh2->ch |= 512 << 16; // mplen
//   qh2->ch |= 2 << 12;   // eps
//   qh2->ch |= 1 << 8;    // eps
//   qh2->ch |= addr;      // eps
//   qh2->cap = 0x40000000 | interrupt;
//
//   intqh->nextTD = 1;
//   intqh->altTD = 1;
//   intqh->qhlp = ((uint32_t)qh2) | 2;
//   intqh->currentTD = 0;
//   intqh->ch = 1 << 15; // T
//   intqh->token = 0x40;
//
//   // 1 frame is equal to 1/8 ms
//   for (int i = 0; i < 1024; i++)
//     framelist[i] = ((uint32_t)intqh) | 2;
//
//   serial_send_string("periodic frame list set\n");
// }
//
// void ehci_send_packet_and_receive(uint8_t addr, usb_setup_packet_t
// *packet,
//                                   void *data, uint8_t interrupt) {
//
//   EhciQTD *cmd = (EhciQTD *)phys_base_alloc(1);
//   EhciQTD *td = (EhciQTD *)phys_base_alloc(1);
//   EhciQTD *status = (EhciQTD *)phys_base_alloc(1);
//   memset(cmd, 0, sizeof(EhciQTD));
//   memset(td, 0, sizeof(EhciQTD));
//   memset(status, 0, sizeof(EhciQTD));
//
//   // cmd->link = (uint32_t)status;
//   cmd->altlink = 1;
//   cmd->token |= (8 << 16);   // cmd size
//   cmd->token |= (1 << 7);    // actief
//   cmd->token |= (0x2 << 8);  // type is cmd
//   cmd->token |= (0x3 << 10); // maxerror
//   cmd->buffer[0] = (uint32_t)packet;
//
//   // if (packet->wLength > 0) {
//   cmd->link = (uint32_t)td;
//   td->altlink = 1;
//   td->link = (uint32_t)status;
//   td->token |= (packet->wLength << 16); // td size
//   td->token |= (1 << 7);                // actief
//   td->token |= (1 << 8);                // type is td
//   td->token |= (0x3 << 10);             // maxerror
//   td->token |= (1 << 31);               // data
//   if (interrupt)
//     td->token |= (1 << 15); // data
//   td->buffer[0] = (uint32_t)data;
//   // }
//
//   status->altlink = 1;
//   status->link = 1;
//   status->token |= (1 << 7);    // actief
//   status->token |= (0 << 8);    // type is td
//   status->token |= (0x3 << 10); // maxerror
//   status->token |= (1 << 31);   // data
//
//   EhciQH *qh = (EhciQH *)phys_base_alloc(1);
//   EhciQH *qh2 = (EhciQH *)phys_base_alloc(1);
//
//   // print qh and qh2 adr
//   serial_send_string("QH : ");
//   serial_send_number((uint32_t)qh, 16);
//   serial_send_string("  QH2 : ");
//   serial_send_number((uint32_t)qh2, 16);
//   serial_send_string("\n");
//
//   memset(qh, 0, sizeof(EhciQH));
//   memset(qh2, 0, sizeof(EhciQH));
//
//   qh2->altTD = 1;
//   qh2->qhlp = ((uint32_t)qh) | 2;
//   qh2->nextTD = (uint32_t)cmd;
//   qh2->currentTD = 0;
//   qh2->ch |= 1 << 14;   // dtc
//   qh2->ch |= 512 << 16; // mplen
//   qh2->ch |= 2 << 12;   // eps
//   qh2->ch |= addr;      // eps
//   qh2->cap = 0x40000000 | interrupt;
//
//   qh->nextTD = 1;
//   qh->altTD = 1;
//   qh->qhlp = ((uint32_t)qh2) | 2;
//   qh->currentTD = 0;
//   qh->ch = 1 << 15; // T
//   qh->token = 0x40;
//
//   op->asynclistaddr = (uint32_t)(uintptr_t)qh;
//   op->usbcmd |= (1 << 5);
//
//   // debug_qh(qh2);
//
//   usleep(100);
//   serial_send_string("proccessing....\n");
//   boolean_t stall = 0;
//   for (int i = 0; i < 10; i++) {
//     usleep(100);
//     uint32_t token = cmd->token;
//     ;
//
//     if (cmd->token & (1 << 6)) {
//       serial_send_string("halted\n");
//       // head->done = 1;
//       serial_send_string("qh token : ");
//       serial_send_number(token, 2);
//       serial_send_string("\n");
//       break;
//     }
//
//     if (cmd->token & (1 << 5)) {
//       serial_send_string("Data Buffer Error\n");
//       // head->done = 1;
//       break;
//     }
//
//     if (cmd->token & (1 << 4)) {
//       serial_send_string("Babble detected\n");
//       // head->done = 1;
//       break;
//     }
//
//     if (cmd->token & (1 << 3)) {
//       serial_send_string("Transaction error\n");
//       // head->done =1 ;
//       break;
//     }
//   }
//
//   op->usbcmd &= ~(1 << 5);
// }
//
// static void ehci_probe(int ports, ehci_operation_t *op) {
//   serial_send_string("EHCI Probing\n");
//   for (uint8_t i = 0; i < ports; i++) {
//     ehci_port_reset(i, op);
//     boolean_t port_enable = (op->portsc[i] & PORT_ENABLED);
//
//     if (port_enable) {
//       serial_send_string("\nUSB Port ");
//       serial_send_number(i, 10);
//       serial_send_string(" Available\n");
//
//       // check port power on
//       serial_send_string("Port Power : ");
//       serial_send_number((op->portsc[i] & PORT_POWER) >> 12, 2);
//       serial_send_string("\n");
//
//       // print descriptor size and address
//       serial_send_string("Descriptor Size : ");
//       serial_send_number(sizeof(usb_device_descriptor_t), 10);
//       serial_send_string("\n");
//
//       serial_send_string("setting an adddr...\n");
//
//       usb_set_addr(addr);
//
//       serial_send_string("getting usb descriptor...\n");
//
//       usb_device_descriptor_t *desc =
//           (usb_device_descriptor_t *)usb_get_descriptor(
//               addr, 1, 0, sizeof(usb_device_descriptor_t));
//       // print dscriptor
//
//       serial_send_string("\nfound uSB descriptor \nusb descriptor length
//       : "); serial_send_number(desc->bLength, 10);
//       serial_send_string("\n");
//       serial_send_string("USB descriptor type : ");
//       serial_send_number(desc->bDescriptorType, 10);
//       serial_send_string("\n");
//       serial_send_string("USB version : ");
//       serial_send_number(desc->bcdUSB, 16);
//       serial_send_string("\n");
//       serial_send_string("USB Device class : ");
//       serial_send_number(desc->bDeviceClass, 10);
//       serial_send_string("  USB Device sub class : ");
//       serial_send_number(desc->bDeviceSubClass, 10);
//       serial_send_string("\n");
//       serial_send_string("USB Device protocol : ");
//       serial_send_number(desc->bDeviceProtocol, 10);
//       serial_send_string(" ");
//       serial_send_string("USB Max packet size : ");
//       serial_send_number(desc->bMaxPacketSize0, 10);
//       serial_send_string(" ");
//       serial_send_string("USB Number of Configuration : ");
//       serial_send_number(desc->bNumConfigurations, 10);
//       serial_send_string("\n vendor id : ");
//       serial_send_number(desc->idVendor, 10);
//       serial_send_string("\n\n");
//
//       uint8_t devClass = desc->bDeviceClass;
//       uint8_t devSubClass = desc->bDeviceSubClass;
//       uint8_t devProtocol = desc->bDeviceProtocol;
//
//       usb_config_descriptor_t *config =
//           (usb_config_descriptor_t *)usb_get_descriptor(
//               addr, 2, 0,
//               sizeof(usb_config_descriptor_t) + sizeof(usb_interface_t) +
//                   sizeof(usb_endpoint_descriptor_t) * 2);
//
//       serial_send_string("number interfaces available : ");
//       serial_send_number(config->bNumInterfaces, 10);
//       serial_send_string("\n index string configurtion : ");
//       serial_send_number(config->iConfiguration, 10);
//       serial_send_string("\n confiuration value : ");
//       serial_send_number(config->bConfigurationValue, 10);
//       serial_send_string("\n");
//
//       usb_interface_t *interface =
//           (usb_interface_t *)((uint32_t)config +
//                               sizeof(usb_config_descriptor_t));
//       serial_send_string("\n\nfound interface uSB descriptor "
//                          "\nusb descriptor length : ");
//       serial_send_number(interface->bLength, 10);
//       serial_send_string("\n");
//       serial_send_string("descriptor type: ");
//       serial_send_number(interface->bDescriptorType, 10);
//       serial_send_string("\n");
//       serial_send_string("interface class : ");
//       serial_send_number(interface->bInterfaceClass, 10);
//       serial_send_string("  USB Device sub class : ");
//       serial_send_number(interface->bInterfaceSubClass, 10);
//       serial_send_string("\n Protocol : ");
//       serial_send_number(interface->bInterfaceProtocol, 10);
//       serial_send_string("\n number endpoint : ");
//       serial_send_number(interface->bNumEndpoints, 10);
//       serial_send_string("\n interface number : ");
//       serial_send_number(interface->bInterfaceNumber, 10);
//       serial_send_string("\n\n");
//
//       // get string descriptor
//       if (config->iConfiguration > 0) {
//         usb_string_descriptor_t *str =
//             (usb_string_descriptor_t *)usb_get_descriptor(
//                 addr, 3, config->iConfiguration,
//                 config->bLength -
//                     (sizeof(usb_config_descriptor_t) +
//                     sizeof(usb_interface_t) +
//                      interface->bNumEndpoints *
//                          sizeof(usb_endpoint_descriptor_t)));
//         serial_send_string("string descriptor : ");
//         serial_send_number(str->bDescriptorType, 10);
//         serial_send_string("\n");
//       }
//
//       // set configuration
//       usb_setup_packet_t *set_config = (usb_setup_packet_t
//       *)phys_base_alloc(1); memset(set_config, 0,
//       sizeof(usb_setup_packet_t)); set_config->bRequest = 0x09;
//       set_config->bmRequestType |= 0;
//       set_config->wValue = config->bConfigurationValue;
//       set_config->wIndex = 0;
//       set_config->wLength = 0;
//       ehci_send_packet_and_receive(addr, set_config, 0, 0);
//       serial_send_string("set configuration\n");
//
//       if (interface->bNumEndpoints > 0) {
//         usb_endpoint_descriptor_t *endpoint =
//             (usb_endpoint_descriptor_t *)(((uint32_t)config) +
//                                           sizeof(usb_config_descriptor_t)
//                                           + sizeof(usb_interface_t));
//         serial_send_string("found uSB descriptor \nusb "
//                            "descriptor length : ");
//         serial_send_number(endpoint->bLength, 10);
//         serial_send_string("\n");
//         serial_send_string("descriptor type: ");
//         serial_send_number(endpoint->bDescriptorType, 10);
//         serial_send_string("\n");
//         serial_send_string("endpoint address : ");
//         serial_send_number(endpoint->bEndpointAddress & 0xF, 2);
//         serial_send_string("  bmAttributes : ");
//         serial_send_number(endpoint->bmAttributes, 2);
//         serial_send_string("  wMaxPacketSize : ");
//         serial_send_number(endpoint->wMaxPacketSize, 10);
//         serial_send_string("  bInterval : ");
//         serial_send_number(endpoint->bInterval, 10);
//         serial_send_string("\n");
//
//         // set iddle
//         usb_setup_packet_t *set_iddle =
//             (usb_setup_packet_t *)phys_base_alloc(1);
//         memset(set_iddle, 0, sizeof(usb_setup_packet_t));
//         set_iddle->bRequest = 0x0A;
//         set_iddle->bmRequestType = 0b00100001;
//         set_iddle->wValue = ((uint16_t)endpoint->bInterval);
//         set_iddle->wIndex = interface->bInterfaceNumber;
//         set_iddle->wLength = 0;
//         ehci_send_packet_and_receive(addr, set_iddle, 0, 0);
//         serial_send_string("setting iddle done\n\n");
//       }
//
//       if (devClass == 0 && devSubClass == 0) {
//         devClass = interface->bInterfaceClass;
//         devSubClass = interface->bInterfaceSubClass;
//         devProtocol = interface->bInterfaceProtocol;
//       }
//
//       devices[devices_index] = (usb_device){.deviceClass = devClass,
//                                             .deviceAddr = addr,
//                                             .deviceSubClass =
//                                             devSubClass, .deviceProtocol
//                                             = devProtocol};
//
//       usb_setup_packet_t *setup_packet =
//           (usb_setup_packet_t *)phys_base_alloc(1);
//       setup_packet->bmRequestType = 0b10000001;
//       setup_packet->bRequest = 0x06;
//       setup_packet->wValue = 0x2200;
//       setup_packet->wIndex = 0x0;
//       setup_packet->wLength = 255;
//       uint8_t *it = (uint8_t *)phys_base_alloc(1);
//       ehci_send_packet_and_receive(addr, setup_packet, it, 1);
//
//       serial_send_string("report : ");
//       for (int i = 0; i < 255; i++) {
//         serial_send_number(it[i], 16);
//         serial_send_string(" ");
//       }
//       serial_send_string("\n");
//
//       if (devClass == USB_CLASS_HID)
//         usb_hid_setup(devices[devices_index]);
//
//       devices_index++;
//       addr++;
//     } else {
//       serial_send_string("Port ");
//       serial_send_number(i, 10);
//       serial_send_string(" Not Available\n");
//     }
//   }
// }
//
// void setup_ehci() {
//   // Find the correct device from PCI
//   for (int i = 0; i < 32; i++) {
//     // TODO: find with propper way
//     uint8_t subclass = pci_devices[i].subclass;
//     uint8_t class = pci_devices[i].class;
//     if (subclass == EHCI_SUBCLASS &&
//         class == EHCI_CLASS) { // EHCI (Not properly implemented but ok
//         for now)
//       ehci_dev = pci_devices[i];
//       serial_send_string("\nFound EHCI device\n");
//       break;
//     }
//   }
//   if (ehci_dev.bar[0] == 0) {
//     serial_send_string("EHCI device not found\n");
//     return;
//   }
//
//   serial_send_string("Found EHCI Base Addr at : 0x");
//   serial_send_number(ehci_dev.bar[0], 16);
//   serial_send_string("\n");
//
//   uint8_t cap_length = *(uint8_t *)(ehci_dev.bar[0]);
//   op = (ehci_operation_t *)(ehci_dev.bar[0] + cap_length);
//   // uint32_t *port0 = (uint32_t *)(ehci_dev.bar[0] + cap_length + 0x44);
//   uint32_t *hcsparam = (uint32_t *)(ehci_dev.bar[0] + 0x4);
//
//   // check is 64 it supported
//   if (*hcsparam & 1) {
//     serial_send_string("64 bit addressing supported\n");
//   }
//
//   // qh_pool = (EhciQH *)higher_half_data_to_phys(
//   //     (uint64_t)phys_base_alloc(1 + (sizeof(EhciQH) * MAX_QUE_HEAD) /
//   //     4096));
//   // qtd_pool = (EhciQTD *)higher_half_data_to_phys(
//   //     (uint64_t)phys_base_alloc(1 + (sizeof(EhciQTD) * MAX_QUE_TD) /
//   //     4096));
//   // memset(qh_pool, 0, sizeof(EhciQH) * MAX_QUE_HEAD);
//   // memset(qtd_pool, 0, sizeof(EhciQTD) * MAX_QUE_TD);
//
//   devices = (usb_device *)phys_base_alloc(1);
//
//   op->usbcmd &= ~CMD_START;
//   while (!(op->usbsts & (1 << 12)))
//     ;
//
//   // Reset EHCI
//   op->usbcmd |= CMD_RESET;
//   while (op->usbcmd & CMD_RESET)
//     ;
//
//   // TODO: check  extended cap
//
//   op->usbintr = 1;
//   op->frindex = 0;
//   op->ctrldssegment = 0;
//   op->usbsts = 0x3f;
//
//   // enable controller
//   op->usbcmd = (0x1 << 16) | 1;
//
//   while ((op->usbsts & (1 << 12)))
//     ;
//
//   op->configflag = 1;
//   serial_send_string("port available : ");
//   int ports = *hcsparam & 0b1111;
//   serial_send_number(ports, 10);
//   serial_send_string("\n");
//
//   // setup periodic
//   EhciQH *qh = (EhciQH *)phys_base_alloc(1);
//   framelist = (uint32_t)phys_base_alloc(1 + ((1024 * sizeof(uint32_t)) /
//   4096)); memset(qh, 0, 1024 * sizeof(EhciQH)); qh->altTD = 1; qh->nextTD
//   = 1; qh->qhlp = ((uint32_t)qh) | 1 | 1 << 1; qh->currentTD = 0; qh->ch
//   = 0; qh->token = 0x40;
//
//   for (int i = 0; i < 1024; i++) {
//     framelist[i] = ((uint32_t)qh) | 1 << 1;
//   }
//
//   op->periodiclistbase = (uint32_t)framelist;
//   op->usbcmd |= (1 << 4);
//
//   itintr = (uint8_t *)phys_base_alloc(1);
//
//   ehci_probe(ports, op);
//
//   // setting up interrupt
//   // ehci_send_packet_interupt
// }
//
// void usb_ehci_interrupt() {
//   // serial_send_string("EHCI Interrupt\n");
//   // serial_send_string("usbsts : ");
//   // serial_send_number(op->usbsts, 2);
//   // serial_send_string("\n");
//   op->usbsts = 0x3f;
//   op->usbintr = 1;
//
//   serial_send_string("report : ");
//   for (int i = 0; i < 8; i++) {
//     serial_send_number(itintr[i], 16);
//     serial_send_string(" ");
//   }
//   serial_send_string("\n");
//   memset(itintr, 0, sizeof(uint32_t));
//   ehci_send_packet_interupt(1, 32, itintr, 1);
// }
