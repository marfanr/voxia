/* #include <firmw/ehci/ehci.h> */
/* #include <firmw/usb/usb.h> */
/* #include <firmw/usb/usb_hid.h> */
/* #include <libk/serial.h> */
/* #include <memory/memory_utils.h> */
/* #include <memory/phys_base_allocator.h> */
/**/
/* void */
/* usb_hid_setup (usb_device device) */
/* { */
/*     // setup usb hid */
/*     serial_send_string ("found an usb hid device\n"); */
/*     serial_send_string ("usbp protocol : "); */
/*     serial_send_number (device.deviceProtocol, 10); */
/*     serial_send_string ("\n"); */
/**/
/*     if (device.deviceProtocol == 1) */
/*         { */
/*             serial_send_string ("found an  keyboard device\n"); */
/*         } */
/*     else if (device.deviceProtocol == 2) */
/*         { */
/*             serial_send_string ("found an mouse device\n"); */
/*         } */
/**/
/*     if (device.deviceSubClass == 0x1) */
/*         { */
/*             serial_send_string ("boot protocol supported\n"); */
/**/
/*             // send set protocol request */
/*             usb_setup_packet_t *setup_packet */
/*                 = (usb_setup_packet_t *)phys_base_alloc (1); */
/*             setup_packet->bmRequestType = 0x21; */
/*             setup_packet->bRequest = 0x0B; */
/*             setup_packet->wValue = 0x1; */
/*             setup_packet->wIndex = 0x0; */
/*             setup_packet->wLength = 0x0; */
/*             ehci_send_packet_and_receive (device.deviceAddr, setup_packet, 0,
 */
/*                                           0); */
/*             phys_base_free (VIRT2PHYS ((uint64_t)setup_packet), 1); */
/*         } */
/**/
/*     //   get hi descriptor */
/* } */
