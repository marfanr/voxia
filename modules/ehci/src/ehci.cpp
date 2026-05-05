#include "ehci/ehci.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_usb.h"
#include "type.h"
#include "usb.h"
#include <stddef.h>
#include <stdint.h>

#define EHCI_MAX_QH_CACHE 128
#define EHCI_MAX_QH_CACHE_MASK (EHCI_MAX_QH_CACHE - 1)
#define EHCI_MAX_QTD_CACHE 512
#define EHCI_MAX_QTD_CACHE_MASK (EHCI_MAX_QTD_CACHE - 1)

// cache
static ehci_queue_head_node_t qh_cache[EHCI_MAX_QH_CACHE];
static size_t qh_cache_head = 0;
static size_t qh_cache_tail = 0;

static ehci_queue_task_descriptor_node_t qtd_cache[EHCI_MAX_QTD_CACHE];
static size_t qtd_cache_head = 0;
static size_t qtd_cache_tail = 0;

// used qh
static ehci_queue_head_node_t* main_qh = 0;

void EHCIModule::init_controller() {

	// init cache
	{
		size_t ehci_qh_block_per_alloc =
			0x1000 / sizeof(ehci_queue_head);
		size_t alloc_count = 0;

		for (size_t i = 0; i < EHCI_MAX_QH_CACHE;
		     i += ehci_qh_block_per_alloc) {

			uintptr_t physaddr = 0;
			uintptr_t vaddr = (uintptr_t) IOUtils::DMAAlloc(
				0x1000, &physaddr);

			if (!vaddr)
				break;

			ioforge_memset((void*) vaddr, 0, 0x1000);

			for (size_t j = 0; j < ehci_qh_block_per_alloc; j++) {
				if ((i + j) >= EHCI_MAX_QH_CACHE)
					break;

				uint32_t offset = j * sizeof(ehci_queue_head);
				struct ehci_queue_head* qh =
					(struct ehci_queue_head*) (vaddr
								   + offset);

				// qh[i+ j]->physaddr = (uint32_t) (physaddr + offset);
				qh_cache[i + j].head = qh;
				qh_cache[i + j].physaddr =
					(uint32_t) (physaddr + offset);
				qh_cache[i + j].next = 0;
			}
			alloc_count++;
		}

		size_t ehci_qtd_block_per_alloc =
			0x1000 / sizeof(ehci_queue_task_descriptor);
		for (size_t i = 0; i < EHCI_MAX_QTD_CACHE;
		     i += ehci_qtd_block_per_alloc) {

			uintptr_t physaddr = 0;
			uintptr_t vaddr = (uintptr_t) IOUtils::DMAAlloc(
				0x1000, &physaddr);

			if (!vaddr)
				break;

			ioforge_memset((void*) vaddr, 0, 0x1000);

			for (int j = 0; j < ehci_qh_block_per_alloc; j++) {
				if ((i + j) >= EHCI_MAX_QH_CACHE)
					break;

				uint32_t offset =
					j * sizeof(ehci_queue_task_descriptor);
				struct ehci_queue_task_descriptor* qtd =
					(struct
					 ehci_queue_task_descriptor*) (vaddr
								       + offset);

				qtd_cache[i + j].physaddr =
					(uint32_t) (physaddr + offset);
				qtd_cache[i + j].task_descriptor = qtd;
				qtd_cache[i + j].next = 0;
			}
			alloc_count++;
		}

		qh_cache_tail = EHCI_MAX_QH_CACHE;
		qtd_cache_tail = EHCI_MAX_QTD_CACHE;

		log(mod, "allocate %d Kb for queue cache (%d QH and %d QTD)",
		    alloc_count * 0x1000 / 1024, EHCI_MAX_QH_CACHE,
		    EHCI_MAX_QTD_CACHE);
	}

	// init first queue head
	ehci_queue_head_node_t* qh_node = 0;
	if (!retrieve_qh(qh_node))
		return;

	// struct ehci_queue_head* qh = qh_node->head;

	// qh->qhlp = EHCI_QTD_TERMINATE | EHCI_Q_SELECT_QH;
	// qh->altTD = EHCI_QTD_TERMINATE;
	// qh->nextTD = EHCI_QTD_TERMINATE;
	// qh->currentTD = 0;
	// qh->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION | 0;

	// log(mod, "first qh: 0x%x", qh);
	// main_qh = qh_node;
	// ehci_op->asynclistaddr = (uint32_t) (uintptr_t) qh_node->physaddr;
	// ehci_op->usbcmd |= EHCI_START_ASYNC_SCHEDULE;
}

void EHCIModule::reset_device() {
	ehci_op->usbcmd |= EHCI_CONTROLLER_RESET;
	while (ehci_op->usbcmd & EHCI_CONTROLLER_RESET) {
		if (ehci_op->usbsts & (1 << 4)) {
			log(mod, "EHCI: reset failed");
			break;
		}
	}
	log(mod, "EHCI: reset engine");
}

void EHCIModule::stop_device() {
	log(mod, "EHCI: stopping_engine");
	ehci_op->usbcmd &= ~(1 << 4);
	ehci_op->usbcmd &= ~(1 << 5);

	while (ehci_op->usbsts & ((1 << 14) | (1 << 15)))
		;

	log(mod, "EHCI: stop_engine");

	ehci_op->usbcmd &= ~1;
	while (!(ehci_op->usbsts & (1 << 12)))
		;
	log(mod, "EHCI: stop_engine");
}

void EHCIModule::start_device() {
	ehci_op->usbcmd |= EHCI_CONTROLLER_START;
	while ((ehci_op->usbsts & EHCI_HC_HALTED_STATUS))
		;
	log(mod, "EHCI: start engine");
}

void EHCIModule::init_periodic() {
	log(mod, "EHCI: init_que_hea");
	uintptr_t qh_phys_addr = 0;
	struct ehci_queue_head* qh_ =
		(struct ehci_queue_head*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_head), &qh_phys_addr);

	log(mod, "qh_ : 0x%x (0x%x)", qh_, qh_phys_addr);

	// init framelist
	uintptr_t framelist_phys_addr = 0;
	framelist = (uint32_t*) IOUtils::DMAAlloc(1024 * sizeof(uint32_t),
						  &framelist_phys_addr);
	IOUtils::memset(framelist, 0, 1024 * sizeof(uint32_t));

	qh_->altTD = 1;
	qh_->nextTD = 1;
	qh_->qhlp = 1;
	qh_->currentTD = 0;
	qh_->ch = 0;
	qh_->token = 0x40;

	for (int i = 0; i < 1024; i++) {
		framelist[i] = ((uint32_t) (uintptr_t) qh_phys_addr) | (1 << 1);
	}

	log(mod, "framelist : 0x%x (0x%x)", framelist, framelist_phys_addr);
	ehci_op->frindex = 0;
	ehci_op->periodiclistbase = (uint32_t) (uintptr_t) framelist_phys_addr;
	ehci_op->usbcmd |= (1 << 4);
}

void EHCIModule::start_periodic() {
	ehci_op->usbcmd |= EHCI_PERIODIC_SCHEDULE_ENABLE;
}

void EHCIModule::stop_periodic() {
	ehci_op->usbcmd &= ~EHCI_PERIODIC_SCHEDULE_ENABLE;
}

#define HCSPARAM_N_PORTS_MASK 0b1111

void EHCIModule::usb_get_string_descriptor(uint8_t addr, uint8_t index,
					   char* data, size_t size) {
	uint8_t* buffer = (uint8_t*) IOUtils::alloc(255);
	usb_get_descriptor(addr, 0x3, index, 255, buffer);

	struct usb_string_descriptor* str =
		(struct usb_string_descriptor*) buffer;

	size_t len = (str->bLength - 2) / 2;

	size_t j = 0;
	for (size_t i = 0; i < len && j < size - 1; i++) {
		uint16_t ch = str->wData[i];

		if (ch < 128) {
			data[j++] = (char) ch;
		}
	}
	data[j] = 0;

	IOUtils::free(buffer, 255);
}

void EHCIModule::probe() {

	if (!controller) {
		log("EHCI", "ERROR: controller must be set");
		return;
	}

	int ports = *hcsparam & HCSPARAM_N_PORTS_MASK;
	log(mod, "EHCI: port available : %d ", ports);
	for (int i = 0; i < ports; i++) {
		uint16_t addr = i + 1;
		port_reset(i);

		boolean_t available = ehci_op->portsc[i] & EHCI_PORT_ENABLED;

		if (available) {
			log(mod, "Port %d Available", i);
			assign_address(addr);

			// save ke ioforge
			struct ioforge_usb_service* usbDevice =
				(struct ioforge_usb_service*) IOUtils::alloc(
					sizeof(struct ioforge_usb_service));

			uint8_t* data = (uint8_t*) IOUtils::alloc(0x1000);
			usb_get_descriptor(addr, 1, 0,
					   sizeof(usb_device_descriptor), data);

			usb_device_descriptor* dev =
				(usb_device_descriptor*) data;

			log(mod, " USB Port %d : Descriptor Length : %d", i,
			    dev->bLength);
			log(mod, " USB Port %d : Descriptor Type : %d", i,
			    dev->bDescriptorType);
			log(mod, " USB Port %d : Version : %x", i, dev->bcdUSB);
			log(mod, " USB Port %d : Device class : %d", i,
			    dev->bDeviceClass);
			log(mod, "  USB Device sub class : %d ",
			    dev->bDeviceSubClass);
			log(mod, "USB Device protocol : %d",
			    dev->bDeviceProtocol);
			log(mod, "USB Max packet size : %d",
			    dev->bMaxPacketSize0);
			log(mod, "USB Number of Configuration : %d",
			    dev->bNumConfigurations);
			log(mod, "USB Vendor ID : %d", dev->idVendor);
			// char iManufacturer[64] = {0};
			// usb_get_string_descriptor(addr, dev->iManufacturer,
			// 			  iManufacturer,
			// 			  sizeof(iManufacturer));
			// log(mod, "USB Manufacturer: %s", iManufacturer);
			// char iProduct[64] = {0};
			// usb_get_string_descriptor(addr, dev->iProduct, iProduct,
			// 			  sizeof(iProduct));
			// log(mod, "USB Product: %s", iProduct);
			// char iSerialNumber[64] = {0};
			// usb_get_string_descriptor(addr, dev->iSerialNumber,
			// 			  iSerialNumber,
			// 			  sizeof(iSerialNumber));
			// log(mod, "USB Serial Number: %s", iSerialNumber);

			uint8_t dev_class = dev->bDeviceClass;
			uint8_t dev_sub_class = dev->bDeviceSubClass;
			uint8_t dev_protocol = dev->bDeviceProtocol;

			uint8_t endpoint_count = 0;

			for (int j = 0; j < dev->bNumConfigurations; j++) {
				IOUtils::memset(data, 0, 0x1000);
				usb_get_descriptor(
					addr, 2, j,
					sizeof(usb_config_descriptor), data);
				usb_config_descriptor* config =
					(usb_config_descriptor*) data;
				usb_get_descriptor(addr, 2, j,
						   config->wTotalLength, data);
				config = (usb_config_descriptor*) data;

				log(mod,
				    " USB Port %d : Descriptor Length : %d", i,
				    config->bLength);
				log(mod, " USB Port %d : Descriptor Type : %d",
				    i, config->bDescriptorType);
				log(mod, " USB Port %d : Total Length : %d", i,
				    config->wTotalLength);
				log(mod,
				    " USB Port %d : Number of Interface : %d",
				    i, config->bNumInterfaces);
				log(mod,
				    " USB Port %d : Configuration Value : %d",
				    i, config->bConfigurationValue);
				log(mod, " USB Port %d : Attribute : %d", i,
				    config->bmAttributes);
				log(mod, " USB Port %d : Max Power : %d", i,
				    config->bMaxPower);
				log(mod, " USB Port %d : iConfiguration : %d",
				    i, config->iConfiguration);

				char iConfiguration[255] = {0};
				usb_get_string_descriptor(
					addr, config->iConfiguration,
					iConfiguration, sizeof(iConfiguration));
				log(mod, "USB Configuration: %s\n",
				    iConfiguration);

				// ioforge
				usbDevice->max_power = config->bMaxPower;
				//

				// interface
				struct usb_interface* interface =
					(struct
					 usb_interface*) ((uintptr_t) config
							  + config->bLength);
				log(mod, " Interface length : %d",
				    interface->bLength);
				log(mod, " Interface type : %d",
				    interface->bDescriptorType);
				log(mod, " Interface class : %d",
				    interface->bInterfaceClass);
				log(mod, "  USB Device sub class : %d ",
				    interface->bInterfaceSubClass);
				log(mod, " Protocol : %d",
				    interface->bInterfaceProtocol);
				log(mod, " number endpoint : %d",
				    interface->bNumEndpoints);

				if (dev_class == 0 && dev_sub_class == 0) {
					dev_class = interface->bInterfaceClass;
					dev_sub_class =
						interface->bInterfaceSubClass;
					dev_protocol =
						interface->bInterfaceProtocol;
				}

				// endpoint
				// Ambil pointer endpoint pertama (berada tepat setelah interface descriptor)
				struct usb_endpoint_descriptor* endpoint = (struct usb_endpoint_descriptor*)((uintptr_t)interface + interface->bLength);

				for (int k = 0; k < interface->bNumEndpoints;
				     k++) {
					// 1. Print semua data untuk endpoint ke-k
					// Menggunakan 'k' agar log-nya dinamis: [Endpoint 0], [Endpoint 1], dst.
					log(mod, " [Endpoint %d] length : %d",
					    k, endpoint->bLength);
					log(mod, " [Endpoint %d] type : %d", k,
					    endpoint->bDescriptorType);
					log(mod,
					    " [Endpoint %d] address : 0x%x", k,
					    endpoint->bEndpointAddress);

					// (Opsional) Ambil atribut untuk tahu ini Bulk, Interrupt, atau Isochronous
					// log(mod, " [Endpoint %d] attributes : 0x%x", k, endpoint->bmAttributes);

					// 2. SETELAH SELESAI, baru majukan pointer untuk iterasi berikutnya
					endpoint =
						(struct
						 usb_endpoint_descriptor*) ((uintptr_t)
										    endpoint
									    + endpoint->bLength);
				}

				// auto& current_endpoint =
				// 	usbDevice->endpoints[endpoint_count];

				// current_endpoint.
				endpoint_count += interface->bNumEndpoints;
			}

			log(mod, "Get report descriptor");
			{
				uintptr_t setup_paddr;
				struct usb_setup_packet* setup =
					(struct usb_setup_packet*)
						IOUtils::DMAAlloc(
							sizeof(usb_setup_packet),
							&setup_paddr);
				setup->bmRequestType = 0b10000001;
				setup->bRequest = 0x06;
				setup->wValue = 0x2200;
				setup->wIndex = 0;
				setup->wLength = 32;

				uintptr_t in_data_paddr;
				uint8_t* in_data = (uint8_t*) IOUtils::DMAAlloc(
					4096, &in_data_paddr);

				send_async_with_response(
					addr, (uint32_t) setup_paddr,
					sizeof(usb_setup_packet), in_data_paddr,
					32);
				log(mod, "Report Descriptor : ");
				for (int i = 0; i < 32; i++) {
					serial2_printf("0x%x ", in_data[i]);
				}
				serial2_printf("\n");
			}

			usbDevice->vendor_id = dev->idVendor;
			usbDevice->product_id = dev->idProduct;
			usbDevice->class_code = dev_class;
			usbDevice->subclass_code = dev_sub_class;
			usbDevice->protocol = dev_protocol;
			usbDevice->usb_version = IoForgeUSB_VERSION_2;
			usbDevice->controller = controller;
			usbDevice->ep_count = endpoint_count;

			usbDevice->addr = addr;

			if (dev_class == 0x3) {
				if (dev_protocol == 1) {
					IOUtils::strcopy(
						(char*) usbDevice->service.name,
						(char*) "Keyboard");
					serial2_printf(
						"HID Device : Keyboard\n");
				} else if (dev_protocol == 2) {
					IOUtils::strcopy(
						(char*) usbDevice->service.name,
						(char*) "Mouse");
					serial2_printf("HID Device : Mouse\n");
				}
			}

			// save to ioforge
		}
	}
}

void EHCIModule::port_reset(int port) {
	ehci_op->portsc[port] |= EHCI_PORT_RESET;
	log(mod, "EHCI: resetting port %d ...", port);
	ehci_op->portsc[port] &= ~EHCI_PORT_RESET;
	IOUtils::sleep(10);
}

void EHCIModule::sendAsync(uint32_t data_phys, size_t size) {
	if (!data_phys || !size) {
		return;
	}

	uintptr_t head_phys_addr = 0;
	struct ehci_queue_head* head =
		(struct ehci_queue_head*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_head), &head_phys_addr);

	uintptr_t head2_phys_addr = 0;
	struct ehci_queue_head* head2 =
		(struct ehci_queue_head*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_head), &head2_phys_addr);

	uintptr_t setup_phys_addr = 0;
	struct ehci_queue_task_descriptor* setup =
		(struct ehci_queue_task_descriptor*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_task_descriptor),
			&setup_phys_addr);

	uintptr_t status_phys_addr = 0;
	struct ehci_queue_task_descriptor* status =
		(struct ehci_queue_task_descriptor*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_task_descriptor),
			&status_phys_addr);

	setup->link = (uint32_t) status_phys_addr;
	setup->altlink = EHCI_QTD_TERMINATE;
	setup->token = EHCI_QTD_TOKEN_LENGTH(size);
	setup->token |= EHCI_QTD_TOKEN_PID_SETUP;
	setup->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
	setup->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
	setup->buffer[0] = data_phys;

	status->altlink = EHCI_QTD_TERMINATE;
	status->link = EHCI_QTD_TERMINATE;
	status->token = EHCI_QTD_TOKEN_PID_IN;
	status->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
	status->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
	status->token |= EHCI_QTD_TOKEN_DATA;
	status->token |= (1 << 15);

	head2->altTD = EHCI_QTD_TERMINATE;
	head2->nextTD = (uint32_t) (uintptr_t) setup_phys_addr;
	head2->qhlp =
		((uint32_t) (uintptr_t) head_phys_addr) | EHCI_Q_SELECT_QH;
	head2->currentTD = 0;
	head2->ch |= EHCI_QH_CAP_DTC;
	head2->ch |= EHCI_QH_CAP_MAX_PACKET_LENGTH(16);
	head2->cap = EHCI_QH_CAP_MULT_1;

	head->qhlp = (uint32_t) head2_phys_addr | EHCI_Q_SELECT_QH;
	head->altTD = EHCI_QTD_TERMINATE;
	head->nextTD = EHCI_QTD_TERMINATE;
	head->currentTD = 0;
	head->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION | 0;

	ehci_op->asynclistaddr = (uint32_t) (uintptr_t) head2_phys_addr;

	is_trasaction_is_running = 1;
	procces_async(status);

	// TODO : freeing
}

// will be deprecated
void EHCIModule::send_async_with_response(uint8_t addr, uint32_t data_phys,
					  size_t size, uint32_t response,
					  size_t response_size) {

	if (!data_phys || !size) {
		return;
	}

	uintptr_t head_phys_addr = 0;
	struct ehci_queue_head* head =
		(struct ehci_queue_head*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_head), &head_phys_addr);

	uintptr_t head2_phys_addr = 0;
	struct ehci_queue_head* head2 =
		(struct ehci_queue_head*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_head), &head2_phys_addr);

	uintptr_t setup_phys_addr = 0;
	struct ehci_queue_task_descriptor* setup =
		(struct ehci_queue_task_descriptor*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_task_descriptor),
			&setup_phys_addr);

	uintptr_t data_phys_addr = 0;
	struct ehci_queue_task_descriptor* data =
		(struct ehci_queue_task_descriptor*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_task_descriptor),
			&data_phys_addr);

	uintptr_t status_phys_addr = 0;
	struct ehci_queue_task_descriptor* status =
		(struct ehci_queue_task_descriptor*) IOUtils::DMAAlloc(
			sizeof(struct ehci_queue_task_descriptor),
			&status_phys_addr);

	setup->link = (uint32_t) data_phys_addr;
	setup->altlink = EHCI_QTD_TERMINATE;
	setup->token = EHCI_QTD_TOKEN_LENGTH(size);   // setup size
	setup->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE; // actief
	setup->token |= EHCI_QTD_TOKEN_PID_SETUP;     // type is setup
	setup->token |= (0x3 << 10);		      // maxerror
	setup->buffer[0] = (uint32_t) data_phys;

	data->link = (uint32_t) status_phys_addr;
	data->altlink = (uint32_t) status_phys_addr;
	// data->altlink = EHCI_QTD_TERMINATE;
	data->token = (response_size << 16);  // setup size
	data->token |= (1 << 7);	      // aktif
	data->token |= (1 << 31);	      // toggle
	data->token |= EHCI_QTD_TOKEN_PID_IN; // type is in
	data->token |= (0x3 << 10);	      // maxerror
	data->buffer[0] = (uint32_t) (uintptr_t) response;

	status->altlink = EHCI_QTD_TERMINATE;
	status->link = EHCI_QTD_TERMINATE;
	status->token = EHCI_QTD_TOKEN_PID_IN;
	status->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
	status->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
	status->token |= EHCI_QTD_TOKEN_DATA;
	status->token |= (1 << 15);

	// head2->altTD = EHCI_QTD_TERMINATE;
	// head2->nextTD = (uint32_t) (uintptr_t) setup_phys_addr;
	// head2->qhlp =
	// 	((uint32_t) (uintptr_t) head_phys_addr) | EHCI_Q_SELECT_QH;
	// head2->currentTD = 0;
	// head2->ch |= EHCI_QH_CAP_DTC;
	// head2->ch |= EHCI_QH_CAP_MAX_PACKET_LENGTH(64);
	// head2->ch |= addr;
	// head2->cap = EHCI_QH_CAP_MULT_1;

	head2->altTD = EHCI_QTD_TERMINATE;
	head2->nextTD = (uint32_t) (uintptr_t) setup_phys_addr;
	head2->qhlp =
		((uint32_t) (uintptr_t) head_phys_addr) | EHCI_Q_SELECT_QH;
	head2->currentTD = 0;
	head2->ch = EHCI_QH_CAP_DTC; // Data Toggle Control (diurus qTD)
	head2->ch |= EHCI_QH_CAP_MAX_PACKET_LENGTH(64); // 🎯 Fix Max Packet 64
	head2->ch |= (2 << 12); // 🎯 Fix High-Speed EPS
	head2->ch |= addr; // Alamat perangkat (biasanya 0 saat awal enumerasi)
	head2->cap = EHCI_QH_CAP_MULT_1;

	head->qhlp = (uint32_t) head2_phys_addr | EHCI_Q_SELECT_QH;
	head->altTD = EHCI_QTD_TERMINATE;
	head->nextTD = EHCI_QTD_TERMINATE;
	head->currentTD = 0;
	head->ch = 0;
	head->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION;
	head->ch |= EHCI_QH_CAP_DTC;
	head->ch |= EHCI_QH_CAP_MAX_PACKET_LENGTH(64);
	head->ch |= (2 << 12);
	head->ch |= 0;
	// head->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION | 0;

	ehci_op->asynclistaddr = (uint32_t) (uintptr_t) head_phys_addr;

	is_trasaction_is_running = 1;
	procces_async(status);
}

void EHCIModule::send_async_with_response2(uint8_t addr, uint32_t data_phys,
					   size_t size, uint32_t response,
					   size_t response_size) {

	if (!data_phys || !size) {
		return;
	}

	ehci_queue_head_node_t* head = 0;
	if (!retrieve_qh(head))
		return;

	ehci_queue_task_descriptor_node_t* setup = 0;
	if (!retrieve_qtd(setup))
		return;

	ehci_queue_task_descriptor_node_t* data = 0;
	if (!retrieve_qtd(data))
		return;

	// uintptr_t status_phys_addr = 0;
	// struct ehci_queue_task_descriptor* status =
	// 	(struct ehci_queue_task_descriptor*) IOUtils::DMAAlloc(
	// 		sizeof(struct ehci_queue_task_descriptor),
	// 		&status_phys_addr);

	// setup->link = (uint32_t) data_phys_addr;
	// setup->altlink = EHCI_QTD_TERMINATE;
	// setup->token = EHCI_QTD_TOKEN_LENGTH(size);   // setup size
	// setup->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE; // actief
	// setup->token |= EHCI_QTD_TOKEN_PID_SETUP;     // type is setup
	// setup->token |= (0x3 << 10);		      // maxerror
	// setup->buffer[0] = (uint32_t) data_phys;

	// data->link = (uint32_t) status_phys_addr;
	// data->altlink = (uint32_t) status_phys_addr;
	// // data->altlink = EHCI_QTD_TERMINATE;
	// data->token = (response_size << 16);  // setup size
	// data->token |= (1 << 7);	      // aktif
	// data->token |= (1 << 31);	      // toggle
	// data->token |= EHCI_QTD_TOKEN_PID_IN; // type is in
	// data->token |= (0x3 << 10);	      // maxerror
	// data->buffer[0] = (uint32_t) (uintptr_t) response;

	// status->altlink = EHCI_QTD_TERMINATE;
	// status->link = EHCI_QTD_TERMINATE;
	// status->token = EHCI_QTD_TOKEN_PID_IN;
	// status->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
	// status->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
	// status->token |= EHCI_QTD_TOKEN_DATA;
	// status->token |= (1 << 15);

	// // head2->altTD = EHCI_QTD_TERMINATE;
	// // head2->nextTD = (uint32_t) (uintptr_t) setup_phys_addr;
	// // head2->qhlp =
	// // 	((uint32_t) (uintptr_t) head_phys_addr) | EHCI_Q_SELECT_QH;
	// // head2->currentTD = 0;
	// // head2->ch |= EHCI_QH_CAP_DTC;
	// // head2->ch |= EHCI_QH_CAP_MAX_PACKET_LENGTH(64);
	// // head2->ch |= addr;
	// // head2->cap = EHCI_QH_CAP_MULT_1;

	// head2->altTD = EHCI_QTD_TERMINATE;
	// head2->nextTD = (uint32_t) (uintptr_t) setup_phys_addr;
	// head2->qhlp =
	// 	((uint32_t) (uintptr_t) head_phys_addr) | EHCI_Q_SELECT_QH;
	// head2->currentTD = 0;
	// head2->ch = EHCI_QH_CAP_DTC; // Data Toggle Control (diurus qTD)
	// head2->ch |= EHCI_QH_CAP_MAX_PACKET_LENGTH(64); // 🎯 Fix Max Packet 64
	// head2->ch |= (2 << 12); // 🎯 Fix High-Speed EPS
	// head2->ch |= addr; // Alamat perangkat (biasanya 0 saat awal enumerasi)
	// head2->cap = EHCI_QH_CAP_MULT_1;

	// head->qhlp = (uint32_t) head2_phys_addr | EHCI_Q_SELECT_QH;
	// head->altTD = EHCI_QTD_TERMINATE;
	// head->nextTD = EHCI_QTD_TERMINATE;
	// head->currentTD = 0;
	// head->ch = 0;
	// head->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION;
	// head->ch |= EHCI_QH_CAP_DTC;
	// head->ch |= EHCI_QH_CAP_MAX_PACKET_LENGTH(64);
	// head->ch |= (2 << 12);
	// head->ch |= 0;
	// // head->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION | 0;

	// ehci_op->asynclistaddr = (uint32_t) (uintptr_t) head_phys_addr;

	// is_trasaction_is_running = 1;
	// procces_async(status);
}

void EHCIModule::procces_async(ehci_queue_task_descriptor* qtd) {
	ehci_op->usbcmd |= EHCI_START_ASYNC_SCHEDULE;

	for (int i = 0; i < 100; i++) {
		// while (is_trasaction_is_running)
		IOUtils::sleep(100);
		// {
		if (qtd->token & (1 << 6)) {
			log(mod, "halted");
			goto proccess_end;
			break;
		} else if (qtd->token & (1 << 5)) {
			log(mod, "Data Buffer Error");
			goto proccess_end;
			break;
		} else if (qtd->token & (1 << 4)) {
			log(mod, "Babble detected");
			goto proccess_end;
			break;
		} else if (qtd->token & (1 << 3)) {
			log(mod, "Transaction error");
			goto proccess_end;
			break;
		} else if (qtd->token & (1 << 2)) {
			log(mod, "Buffer error");
			goto proccess_end;
			break;
		} else {
			// log(mod, "Success");
			// status len
			// size_t len = qtd->token & 0xffff;
			// log(mod, "len : %d", len);
			goto proccess_end;
			break;
		}
	}
proccess_end:
	ehci_op->usbcmd &= ~EHCI_START_ASYNC_SCHEDULE;
}

void EHCIModule::assign_address(int address) {
	uintptr_t cmd_phys_addr = 0;
	usb_setup_packet* cmd = (usb_setup_packet*) IOUtils::DMAAlloc(
		sizeof(usb_setup_packet), &cmd_phys_addr);
	cmd->bmRequestType = 0;
	cmd->bRequest = USB_SETUP_PACKET_SET_ADDRESS;
	cmd->bmRequestType = 0;
	cmd->wValue = address;
	cmd->wIndex = 0;
	cmd->wLength = 0;
	log(mod, "cmd : 0x%x", cmd_phys_addr);

	sendAsync((uint32_t) cmd_phys_addr, sizeof(usb_setup_packet));
}

void EHCIModule::usb_get_descriptor(uint8_t addr, uint8_t type, uint8_t index,
				    uint8_t len, uint8_t* data) {
	uintptr_t cmd_phys_addr = 0;
	struct usb_setup_packet* cmd =
		(struct usb_setup_packet*) IOUtils::DMAAlloc(
			sizeof(struct usb_setup_packet), &cmd_phys_addr);
	cmd->bRequest = 0x06;
	cmd->bmRequestType = 0x80; // recieve
	cmd->wValue = (type << 8) | index;
	cmd->wIndex = 0;
	cmd->wLength = len;
	// log(mod, "cmd : 0x%x", cmd_phys_addr);

	size_t aligned_size = (len + 0x1000 - 1) / 0x1000;
	uintptr_t data_phys_addr = 0;
	uint8_t* data_ = (uint8_t*) IOUtils::DMAAlloc(aligned_size * 0x1000,
						      &data_phys_addr);
	// log(mod, "data : 0x%x", data_phys_addr);
	send_async_with_response(addr, (uint32_t) cmd_phys_addr,
				 sizeof(usb_setup_packet), data_phys_addr, len);

	IOUtils::memcpy((void*) data, (void*) data_, len);

	IOUtils::DMAFree((void*) data_phys_addr, data_, aligned_size);
}

void EHCIModule::fireHandler() {
	EHCIModule* module = EHCIModule::getInstance();
	if (!module)
		return;

	auto status = module->ehci_op->usbsts;
	if (!(status & 0x3f))
		return;

	if (status & (1 << 0)) {
		// USBINT → transfer complete
		log("EHCI IRQ", "transfer complete");
		if (module->is_trasaction_is_running)
			module->is_trasaction_is_running = 0;
	}

	if (status & (1 << 2)) {
		// PCD → port change
		log("EHCI IRQ", "port change");
	}

	module->ehci_op->usbsts = status;
}

void EHCIModule::set_controller(ioforge_usb_controller_service* controller) {
	this->controller = controller;
}

// cache
boolean_t EHCIModule::retrieve_qh(ehci_queue_head_node_t* out) {
	size_t h = __atomic_load_n(&qh_cache_head, __ATOMIC_ACQUIRE);
	size_t t = __atomic_load_n(&qh_cache_tail, __ATOMIC_ACQUIRE);

	if (h == t) {
		serial2_printf("retrieve_qh: EMPTY\n");
		return false;
	}
	*out = qh_cache[h & EHCI_MAX_QH_CACHE_MASK];

	__atomic_store_n(&qh_cache_head, h + 1, __ATOMIC_RELEASE);
	return true;
}

boolean_t EHCIModule::retrieve_qtd(ehci_queue_task_descriptor_node_t* out) {
	size_t h = __atomic_load_n(&qtd_cache_head, __ATOMIC_ACQUIRE);
	size_t t = __atomic_load_n(&qtd_cache_tail, __ATOMIC_ACQUIRE);

	if (h == t)
		return false;

	*out = qtd_cache[h & EHCI_MAX_QTD_CACHE_MASK];

	__atomic_store_n(&qtd_cache_head, h + 1, __ATOMIC_RELEASE);
	return true;
}

void EHCIModule::store_qh(ehci_queue_head_node_t* in) {
	size_t t = __atomic_load_n(&qh_cache_tail, __ATOMIC_RELAXED);
	size_t h = __atomic_load_n(&qh_cache_head, __ATOMIC_ACQUIRE);

	if (t - h >= EHCI_MAX_QH_CACHE) {
		serial2_printf("[EHCI] store_qh: FULL, drop vaddr=%p\n", in);
		return;
	}

	size_t slot = t & EHCI_MAX_QH_CACHE_MASK;
	qh_cache[slot] = *in;

	__atomic_store_n(&qh_cache_tail, t + 1, __ATOMIC_RELEASE);
}

void EHCIModule::store_qtd(ehci_queue_task_descriptor_node_t* in) {
	size_t h = __atomic_load_n(&qtd_cache_head, __ATOMIC_ACQUIRE);
	size_t t = __atomic_load_n(&qtd_cache_tail, __ATOMIC_RELAXED);

	if (t - h >= EHCI_MAX_QTD_CACHE) {
		serial2_printf("[EHCI] store_qh: FULL, drop vaddr=%p\n", in);
		return;
	}

	size_t slot = t & EHCI_MAX_QTD_CACHE_MASK;
	qtd_cache[slot] = *in;

	__atomic_store_n(&qtd_cache_tail, t + 1, __ATOMIC_RELEASE);
}

// hanya software link, tetep perlu atur flag manual
void EHCIModule::push_to_qh(ehci_queue_head_node_t* qh) {
	auto last = &main_qh;
	while (*last) {
		last = &(*last)->next;
	}
	*last = qh;
}