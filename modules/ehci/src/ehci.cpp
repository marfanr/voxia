#include "ehci/ehci.hpp"
#include "ehci/ehci_pipe.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_usb.h"
#include "memory/kalloc.h"
#include "usb.h"
#include <ioforge/ioforge_new.hpp>
#include <type.h>

#define EHCI_MAX_QH_CACHE 4096
#define EHCI_MAX_QH_CACHE_MASK (EHCI_MAX_QH_CACHE - 1)
#define EHCI_MAX_QTD_CACHE 4096
#define EHCI_MAX_QTD_CACHE_MASK (EHCI_MAX_QTD_CACHE - 1)

static ehci_queue_head_node_t qh_pool[EHCI_MAX_QH_CACHE];
static uint16_t qh_ring[EHCI_MAX_QH_CACHE];
static size_t qh_ring_head = 0;
static size_t qh_ring_tail = 0;

static ehci_queue_task_descriptor_node_t qtd_pool[EHCI_MAX_QTD_CACHE];
static uint16_t qtd_ring[EHCI_MAX_QTD_CACHE];
static size_t qtd_ring_head = 0;
static size_t qtd_ring_tail = 0;

static ehci_queue_head_node_t* framelist_node[1024];
static ehci_queue_head_node_t* main_async_qh = 0;

void EHCIModule::init_controller() {
	static_assert(sizeof(ehci_queue_head) % 32 == 0,
	              "ehci_queue_head must be 32-byte aligned");
	static_assert(sizeof(ehci_queue_task_descriptor) % 32 == 0,
	              "ehci_queue_task_descriptor must be 32-byte aligned");

	size_t alloc_count = 0;

	/* QH pool*/
	{
		size_t block = 0x1000 / sizeof(ehci_queue_head);
		for (size_t i = 0; i < EHCI_MAX_QH_CACHE;
		     i += block, alloc_count++) {
			uintptr_t physaddr = 0;
			uintptr_t vaddr =
			    (uintptr_t)IOUtils::DMAAlloc(0x1000, &physaddr);
			if (!vaddr)
				break;
			ioforge_memset((void*)vaddr, 0, 0x1000);
			for (size_t j = 0; j < block; j++) {
				if ((i + j) >= EHCI_MAX_QH_CACHE)
					break;
				uint32_t off = j * sizeof(ehci_queue_head);
				auto* qh =
				    (struct ehci_queue_head*)(vaddr + off);
				qh_pool[i + j].head = qh;
				qh_pool[i + j].physaddr =
				    (uint32_t)(physaddr + off);
				qh_pool[i + j].next = 0;
				qh_ring[i + j] = i + j;
			}
		}
		qh_ring_head = 0;
		qh_ring_tail = EHCI_MAX_QH_CACHE;
	}

	/* QTD pool */
	{
		size_t block = 0x1000 / sizeof(ehci_queue_task_descriptor);
		for (size_t i = 0; i < EHCI_MAX_QTD_CACHE;
		     i += block, alloc_count++) {
			uintptr_t physaddr = 0;
			uintptr_t vaddr =
			    (uintptr_t)IOUtils::DMAAlloc(0x1000, &physaddr);
			if (!vaddr)
				break;
			ioforge_memset((void*)vaddr, 0, 0x1000);
			for (size_t j = 0; j < block; j++) {
				if ((i + j) >= EHCI_MAX_QTD_CACHE)
					break;
				uint32_t off =
				    j * sizeof(ehci_queue_task_descriptor);
				auto* qtd =
				    (struct ehci_queue_task_descriptor*)(vaddr +
				                                         off);
				qtd_pool[i + j].physaddr =
				    (uint32_t)(physaddr + off);
				qtd_pool[i + j].task_descriptor = qtd;
				qtd_pool[i + j].next = 0;
				qtd_ring[i + j] = i + j;
			}
		}
		qtd_ring_head = 0;
		qtd_ring_tail = EHCI_MAX_QTD_CACHE;

		log(mod, "allocate %d Kb for queue cache (%d QH and %d QTD)",
		    alloc_count * 0x1000 / 1024, EHCI_MAX_QH_CACHE,
		    EHCI_MAX_QTD_CACHE);
	}

	/* Alignment check */
	{
		uint32_t p0 = qh_pool[0].physaddr, p1 = qh_pool[1].physaddr;
		if ((p1 - p0) % 32 != 0) {
			log(mod,
			    "ERROR: QH alignment salah! sizeof=%d, diff=%d",
			    sizeof(ehci_queue_head), p1 - p0);
			return;
		}
		uint32_t q0 = qtd_pool[0].physaddr, q1 = qtd_pool[1].physaddr;
		if ((q1 - q0) % 32 != 0) {
			log(mod,
			    "ERROR: qTD alignment salah! sizeof=%d, diff=%d",
			    sizeof(ehci_queue_task_descriptor), q1 - q0);
			return;
		}
		log(mod, "alignment OK — QH stride=%d, qTD stride=%d", p1 - p0,
		    q1 - q0);
	}

	/* Main async QH */
	ehci_queue_head_node_t* qh_node = 0;
	if (!retrieve_qh(&qh_node))
		return;

	auto* qh = qh_node->head;
	qh->qhlp = qh_node->physaddr | EHCI_Q_SELECT_QH;
	qh->altTD = EHCI_QTD_TERMINATE;
	qh->nextTD = EHCI_QTD_TERMINATE;
	qh->currentTD = 0;
	qh->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION | 0;

	log(mod, "first qh: 0x%x", qh);
	main_async_qh = qh_node;
	ehci_op->asynclistaddr = (uint32_t)(uintptr_t)qh_node->physaddr;
	ehci_op->usbcmd |= EHCI_START_ASYNC_SCHEDULE;
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
	while (ehci_op->usbsts & EHCI_HC_HALTED_STATUS)
		;
	log(mod, "EHCI: start engine");
}

void EHCIModule::init_periodic() {
	log(mod, "periodic: init_que_head");

	uintptr_t framelist_phys_addr = 0;
	framelist = (uint32_t*)IOUtils::DMAAlloc(1024 * sizeof(uint32_t),
	                                         &framelist_phys_addr);
	IOUtils::memset(framelist, 0, 1024 * sizeof(uint32_t));

	ehci_queue_head_node_t* qh_node = 0;
	retrieve_qh(&qh_node);
	auto* qh = qh_node->head;
	IOUtils::memset(qh, 0, sizeof(*qh));
	qh->qhlp = qh_node->physaddr | EHCI_Q_SELECT_QH;
	qh->altTD = EHCI_QTD_TERMINATE;
	qh->nextTD = EHCI_QTD_TERMINATE;
	qh->currentTD = 0;
	qh->token = 0;
	qh->ch = EHCI_QH_CAP_HEAD_OF_RECLAMATION;
	qh->cap = 0xFF;

	for (int i = 0; i < 1024; i++) {
		framelist[i] =
		    ((uint32_t)(uintptr_t)qh_node->physaddr) | EHCI_Q_SELECT_QH;
		framelist_node[i] = qh_node;
	}

	log(mod, "framelist : 0x%x (0x%x)", framelist, framelist_phys_addr);
	ehci_op->frindex = 0;
	ehci_op->periodiclistbase = (uint32_t)(uintptr_t)framelist_phys_addr;
	start_periodic();
}

void EHCIModule::insert_periodic(ehci_queue_head_node_t* qh_node,
                                 uint16_t interval_ms) {
	spin_acquire(&schedule_lock);
	int ring = 0;
	auto curr_node = framelist_node[ring];
	auto* mq = curr_node->head;
	uint32_t saved_next = mq->qhlp;
	qh_node->head->qhlp = saved_next;
	__sync_synchronize();
	mq->qhlp = (uint32_t)qh_node->physaddr | EHCI_Q_SELECT_QH;
	spin_release(&schedule_lock);

	log(mod, "QH dengan interval %d ms dimasukkan ke slot %d", interval_ms,
	    ring);
}

void EHCIModule::start_periodic() {
	ehci_op->usbcmd |= EHCI_PERIODIC_SCHEDULE_ENABLE;
}
void EHCIModule::stop_periodic() {
	ehci_op->usbcmd &= ~EHCI_PERIODIC_SCHEDULE_ENABLE;
}

/* String descriptor helper */

void EHCIModule::usb_get_string_descriptor(uint8_t addr, uint8_t index,
                                           char* data, size_t size) {
	uint8_t* buffer = (uint8_t*)kalloc(255);
	usb_get_descriptor(addr, 0x3, index, 255, buffer);
	auto* str = (struct usb_string_descriptor*)buffer;
	size_t len = (str->bLength - 2) / 2;
	size_t j = 0;
	for (size_t i = 0; i < len && j < size - 1; i++) {
		uint16_t ch = str->wData[i];
		if (ch < 128)
			data[j++] = (char)ch;
	}
	data[j] = 0;
	IOUtils::free(buffer, 255);
}

#define HCSPARAM_N_PORTS_MASK 0b1111

void EHCIModule::probe() {
	if (!controller) {
		log("EHCI", "ERROR: controller must be set");
		return;
	}

	int ports = *hcsparam & HCSPARAM_N_PORTS_MASK;
	log("DEBUG", "OK");
	log(mod, "EHCI: port available : %d ", ports);

	for (int i = 0; i < ports; i++) {
		uint16_t addr = i + 1;
		port_reset(i);

		boolean_t available = ehci_op->portsc[i] & EHCI_PORT_ENABLED;
		if (!available)
			continue;

		assign_address(addr);
		log(mod, "Port %d Available", i);

		auto* usbDevice = (struct ioforge_usb_device*)kalloc(
		    sizeof(struct ioforge_usb_device));
		IOUtils::memset(usbDevice, 0, sizeof(*usbDevice));

		uint8_t* data = (uint8_t*)kalloc(0x1000);
		usb_get_descriptor(addr, 1, 0, sizeof(usb_device_descriptor),
		                   data);
		auto* dev = (usb_device_descriptor*)data;

		log(mod, " USB Port %d : Descriptor Length : %d", i,
		    dev->bLength);
		log(mod, " USB Port %d : Descriptor Type : %d", i,
		    dev->bDescriptorType);
		log(mod, " USB Port %d : Version : %x", i, dev->bcdUSB);
		log(mod, " USB Port %d : Device class : %d", i,
		    dev->bDeviceClass);
		log(mod, "  USB Device sub class : %d ", dev->bDeviceSubClass);
		log(mod, "USB Device protocol : %d", dev->bDeviceProtocol);
		log(mod, "USB Max packet size : %d", dev->bMaxPacketSize0);
		log(mod, "USB Number of Configuration : %d",
		    dev->bNumConfigurations);
		log(mod, "USB Vendor ID : %d", dev->idVendor);

		char iManufacturer[64] = {0};
		usb_get_string_descriptor(addr, dev->iManufacturer,
		                          iManufacturer, sizeof(iManufacturer));
		log(mod, "USB Manufacturer: %s", iManufacturer);

		char iProduct[64] = {0};
		usb_get_string_descriptor(addr, dev->iProduct, iProduct,
		                          sizeof(iProduct));
		log(mod, "USB Product: %s", iProduct);

		char iSerialNumber[64] = {0};
		if (dev->iSerialNumber != 0)
			usb_get_string_descriptor(addr, dev->iSerialNumber,
			                          iSerialNumber,
			                          sizeof(iSerialNumber));
		else
			IOUtils::strcopy(iSerialNumber, (char*)"None");
		log(mod, "USB Serial Number: %s", iSerialNumber);
		IOUtils::strcopy((char*)usbDevice->serial_number,
		                 iSerialNumber);

		uint8_t dev_class = dev->bDeviceClass;
		uint8_t dev_sub_class = dev->bDeviceSubClass;
		uint8_t dev_protocol = dev->bDeviceProtocol;
		uint8_t endpoint_count = 0;

		for (int j = 0; j < dev->bNumConfigurations; j++) {
			IOUtils::memset(data, 0, 0x1000);
			usb_get_descriptor(addr, 2, j,
			                   sizeof(usb_config_descriptor), data);
			auto* config = (usb_config_descriptor*)data;
			usb_get_descriptor(addr, 2, j, config->wTotalLength,
			                   data);
			config = (usb_config_descriptor*)data;

			log(mod, " USB Port %d : Descriptor Length : %d", i,
			    config->bLength);
			log(mod, " USB Port %d : Descriptor Type : %d", i,
			    config->bDescriptorType);
			log(mod, " USB Port %d : Total Length : %d", i,
			    config->wTotalLength);
			log(mod, " USB Port %d : Number of Interface : %d", i,
			    config->bNumInterfaces);
			log(mod, " USB Port %d : Configuration Value : %d", i,
			    config->bConfigurationValue);
			log(mod, " USB Port %d : Attribute : %d", i,
			    config->bmAttributes);
			log(mod, " USB Port %d : Max Power : %d", i,
			    config->bMaxPower);
			log(mod, " USB Port %d : iConfiguration : %d", i,
			    config->iConfiguration);

			char iConfiguration[255] = {0};
			usb_get_string_descriptor(addr, config->iConfiguration,
			                          iConfiguration,
			                          sizeof(iConfiguration));
			log(mod, "USB Configuration Name: %s\n",
			    iConfiguration);
			usbDevice->max_power = config->bMaxPower;

			auto* interface =
			    (struct usb_interface*)((uintptr_t)config +
			                            config->bLength);
			log(mod, "[Interface] Interface length : %d",
			    interface->bLength);
			log(mod, "[Interface] Interface number : %d",
			    interface->bInterfaceNumber);
			log(mod, "[Interface] Interface type : %d",
			    interface->bDescriptorType);
			log(mod, "[Interface] Interface class : %d",
			    interface->bInterfaceClass);
			log(mod, "[Interface]  USB Device sub class : %d ",
			    interface->bInterfaceSubClass);
			log(mod, "[Interface] Protocol : %d",
			    interface->bInterfaceProtocol);
			log(mod, "[Interface] number endpoint : %d",
			    interface->bNumEndpoints);

			if (dev_class == 0 && dev_sub_class == 0) {
				dev_class = interface->bInterfaceClass;
				dev_sub_class = interface->bInterfaceSubClass;
				dev_protocol = interface->bInterfaceProtocol;
			}

			uint8_t* ptr = (uint8_t*)((uintptr_t)interface +
			                          interface->bLength);
			for (int k = 0; k < interface->bNumEndpoints; k++) {
				while (ptr <
				       (uint8_t*)data + config->wTotalLength) {
					uint8_t desc_len = ptr[0];
					uint8_t desc_type = ptr[1];
					if (desc_type == 0x05)
						break;
					if (desc_len == 0)
						break;
					ptr += desc_len;
				}

				auto* endpoint =
				    (struct usb_endpoint_descriptor*)ptr;
				log(mod, " [Endpoint %d] length : %d", k,
				    endpoint->bLength);
				log(mod, " [Endpoint %d] type : %d", k,
				    endpoint->bDescriptorType);
				log(mod, " [Endpoint %d] address : 0x%x", k,
				    endpoint->bEndpointAddress);
				log(mod, " [Endpoint %d] attributes : 0x%x", k,
				    endpoint->bmAttributes);
				log(mod, " [Endpoint %d] interval : %d", k,
				    endpoint->bInterval);

				auto& ep = usbDevice->endpoints[endpoint_count];
				ep.address = endpoint->bEndpointAddress;
				ep.attributes = endpoint->bmAttributes;
				ep.interval = endpoint->bInterval;
				ep.max_packet = endpoint->wMaxPacketSize;

				ptr += endpoint->bLength;
				endpoint_count++;
			}
		}

		/* Get report descriptor */
		{
			uintptr_t setup_paddr;
			auto* setup =
			    (struct usb_setup_packet*)IOUtils::DMAAlloc(
			        sizeof(usb_setup_packet), &setup_paddr);
			setup->bmRequestType = 0b10000001;
			setup->bRequest = 0x06;
			setup->wValue = 0x2200;
			setup->wIndex = 0;
			setup->wLength = 32;

			uintptr_t in_data_paddr;
			uint8_t* in_data =
			    (uint8_t*)IOUtils::DMAAlloc(4096, &in_data_paddr);

			send_async_with_response(addr, 0, (uint32_t)setup_paddr,
			                         sizeof(usb_setup_packet),
			                         in_data_paddr, 32);

			log(mod, "Report Descriptor : ");
			for (int ri = 0; ri < 32; ri++)
				serial2_printf("0x%x ", in_data[ri]);
			serial2_printf("\n");

			IOUtils::DMAFree((void*)in_data_paddr, in_data, 4096);
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
				IOUtils::strcopy((char*)usbDevice->base.name,
				                 (char*)"Keyboard");
				serial2_printf("HID Device : Keyboard\n");
			} else if (dev_protocol == 2) {
				IOUtils::strcopy((char*)usbDevice->base.name,
				                 (char*)"Mouse");
				serial2_printf("HID Device : Mouse\n");
			}
		}

		usbDevice->base.type = IOFORGE_USB_DEVICE;

		auto __aligned_pipe_size = ((sizeof(EHCIPipe) + 31) & ~31);
		void* mem = kalloc(__aligned_pipe_size);
		EHCIPipe* pipe = new (mem) EHCIPipe(this);
		usbDevice->pipe = pipe;

		ioforge_attach(ioforge_get_usb_devices_root(),
		               &usbDevice->base);

		IOUtils::free(data, 0x1000);
	}
}

void EHCIModule::port_reset(int port) {
	uint32_t val = ehci_op->portsc[port];
	val &= ~(0x2A);
	ehci_op->portsc[port] = val | EHCI_PORT_RESET;
	log(mod, "EHCI: resetting port %d ...", port);
	ehci_op->portsc[port] = val & ~EHCI_PORT_RESET;
	IOUtils::sleep(10);
}

void EHCIModule::send_async_with_response(uint8_t addr, uint8_t endpoint,
                                          uint32_t data_phys, size_t size,
                                          uint32_t response,
                                          size_t response_size) {
	if (!data_phys || !size)
		return;

	ehci_queue_head_node_t* qh_node;
	retrieve_qh(&qh_node);
	auto* qh = qh_node->head;
	IOUtils::memset(qh, 0, sizeof(struct ehci_queue_head));

	ehci_queue_task_descriptor_node_t* setup_node;
	retrieve_qtd(&setup_node);
	auto* setup = setup_node->task_descriptor;
	IOUtils::memset(setup, 0, sizeof(struct ehci_queue_task_descriptor));

	ehci_queue_task_descriptor_node_t* status_node;
	retrieve_qtd(&status_node);
	auto* status = status_node->task_descriptor;
	IOUtils::memset(status, 0, sizeof(struct ehci_queue_task_descriptor));

	ehci_queue_task_descriptor_node_t* data_node = 0;

	if (response && response_size > 0) {
		retrieve_qtd(&data_node);
		auto* data_qtd = data_node->task_descriptor;
		IOUtils::memset(data_qtd, 0,
		                sizeof(struct ehci_queue_task_descriptor));

		setup->link = (uint32_t)data_node->physaddr;
		setup->altlink = EHCI_QTD_TERMINATE;
		setup->token =
		    EHCI_QTD_TOKEN_LENGTH(size) | EHCI_QTD_TOKEN_STATUS_ACTIVE |
		    EHCI_QTD_TOKEN_PID_SETUP | EHCI_QTD_TOKEN_ERROR_COUNT_3;
		setup->buffer[0] = (uint32_t)data_phys;

		data_qtd->link = (uint32_t)status_node->physaddr;
		data_qtd->altlink = (uint32_t)status_node->physaddr;
		data_qtd->token = EHCI_QTD_TOKEN_LENGTH(response_size) |
		                  EHCI_QTD_TOKEN_STATUS_ACTIVE |
		                  EHCI_QTD_TOKEN_DATA | EHCI_QTD_TOKEN_PID_IN |
		                  EHCI_QTD_TOKEN_ERROR_COUNT_3;
		data_qtd->buffer[0] = (uint32_t)response;

		status->link = EHCI_QTD_TERMINATE;
		status->altlink = EHCI_QTD_TERMINATE;
		status->token =
		    EHCI_QTD_TOKEN_LENGTH(0) | EHCI_QTD_TOKEN_STATUS_ACTIVE |
		    EHCI_QTD_TOKEN_DATA | EHCI_QTD_TOKEN_PID_OUT |
		    EHCI_QTD_TOKEN_ERROR_COUNT_3 | EHCI_QTD_TOKEN_IOC;
	} else {
		setup->link = (uint32_t)status_node->physaddr;
		setup->altlink = EHCI_QTD_TERMINATE;
		setup->token =
		    EHCI_QTD_TOKEN_LENGTH(size) | EHCI_QTD_TOKEN_STATUS_ACTIVE |
		    EHCI_QTD_TOKEN_PID_SETUP | EHCI_QTD_TOKEN_ERROR_COUNT_3;
		setup->buffer[0] = (uint32_t)data_phys;

		status->link = EHCI_QTD_TERMINATE;
		status->altlink = EHCI_QTD_TERMINATE;
		status->token =
		    EHCI_QTD_TOKEN_LENGTH(0) | EHCI_QTD_TOKEN_STATUS_ACTIVE |
		    EHCI_QTD_TOKEN_DATA | EHCI_QTD_TOKEN_PID_IN |
		    EHCI_QTD_TOKEN_ERROR_COUNT_3 | EHCI_QTD_TOKEN_IOC;
	}

	qh->altTD = EHCI_QTD_TERMINATE;
	qh->nextTD = (uint32_t)setup_node->physaddr;
	qh->currentTD = 0;
	qh->ch = EHCI_QH_CAP_DTC | EHCI_QH_CAP_MAX_PACKET_LENGTH(64) |
	         (2 << 12) /* EPS = High Speed */
	         | (addr & 0x7f);
	qh->cap = EHCI_QH_CAP_MULT_1 | ((endpoint & 0xF) << 8);

	push_to_qh(qh_node);

	/* Poll until status finished */
	bool done = false;
	for (int i = 0; i < 500 && !done; i++) {
		IOUtils::sleep(10);
		uint32_t tok = status->token;
		if (tok & EHCI_QTD_TOKEN_STATUS_ACTIVE)
			continue;
		if (tok & (1 << 6))
			log(mod, "send_async: HALTED");
		if (tok & (1 << 5))
			log(mod, "send_async: Data Buffer Error");
		if (tok & (1 << 4))
			log(mod, "send_async: Babble");
		if (tok & (1 << 3))
			log(mod, "send_async: Transaction Error");
		done = true;
	}
	if (!done)
		log(mod, "send_async: TIMEOUT");

	/* Detach QH dari async schedule */
	spin_acquire(&schedule_lock);
	auto* mq = main_async_qh->head;
	__sync_synchronize();
	mq->qhlp = qh->qhlp;
	spin_release(&schedule_lock);

	/* Async advance doorbell */
	ehci_op->usbcmd |= (1 << 6);
	int timeout = 1000;
	while (!(ehci_op->usbsts & (1 << 5)) && timeout-- > 0)
		IOUtils::sleep(1);
	if (timeout <= 0)
		log(mod, "ERROR: Asynchronous Advance Doorbell timeout!");
	ehci_op->usbsts = (1 << 5);

	store_qh(&qh_node);
	store_qtd(&setup_node);
	store_qtd(&status_node);
	if (data_node)
		store_qtd(&data_node);
}

void EHCIModule::call_completion_callback(ioforge_device* dev) {
	if (!dev)
		return;

	if (dev->type == IOFORGE_USB_DEVICE) {
		auto* usb = (ioforge_usb_device*)dev;
		if (usb->pipe != nullptr) {
			EHCIPipe* pipe = (EHCIPipe*)usb->pipe;
			if (pipe->data_node_) {
				auto* qtd = pipe->data_node_->task_descriptor;
				uint32_t tok = qtd->token;

				if (tok & EHCI_QTD_TOKEN_STATUS_ACTIVE)
					goto next; /* masih jalan, skip */

				bool is_error = false;
				if (tok & (1 << 6)) {
					log("EHCI IRQ", "→ HALTED");
					is_error = true;
				}
				if (tok & (1 << 5)) {
					log("EHCI IRQ", "→ Data Buffer Error");
					is_error = true;
				}
				if (tok & (1 << 3)) {
					log("EHCI IRQ", "→ Transaction Error");
					is_error = true;
				}

				pipe->on_complete(0, 0, is_error);
			}
		}
	}

next:
	call_completion_callback(dev->first_child);
	call_completion_callback(dev->next_sibling);
}

void EHCIModule::fireHandler() {

	EHCIModule* module = EHCIModule::getInstance();
	if (!module)
		return;

	uint32_t status = module->ehci_op->usbsts;
	uint32_t intr_enable = module->ehci_op->usbintr;

	uint32_t handled = status & (intr_enable | 0x17);
	if (!handled)
		return;

	serial2_printf("fired\n");

	module->ehci_op->usbsts = handled;
	__sync_synchronize();

	if (status & (1 << 0)) {
		/* USBINT — transfer complete */
		auto* node = ioforge_get_usb_devices_root();
		module->call_completion_callback(node);
	}

	if (status & (1 << 2)) {
		log("EHCI IRQ", "port change");
	}

	if (status & (1 << 4)) {
		log("EHCI IRQ", "host system error");
	}
}

void EHCIModule::procces_async(ehci_queue_task_descriptor* qtd) {
	ehci_op->usbcmd |= EHCI_START_ASYNC_SCHEDULE;
	for (int i = 0; i < 100; i++) {
		IOUtils::sleep(100);
		if (qtd->token & (1 << 6)) {
			log(mod, "halted");
			break;
		} else if (qtd->token & (1 << 5)) {
			log(mod, "Data Buffer Error");
			break;
		} else if (qtd->token & (1 << 4)) {
			log(mod, "Babble detected");
			break;
		} else if (qtd->token & (1 << 3)) {
			log(mod, "Transaction error");
			break;
		} else if (qtd->token & (1 << 2)) {
			log(mod, "Buffer error");
			break;
		} else {
			break;
		}
	}
	ehci_op->usbcmd &= ~EHCI_START_ASYNC_SCHEDULE;
}

void EHCIModule::assign_address(int address) {
	uintptr_t cmd_phys_addr = 0;
	auto* cmd = (usb_setup_packet*)IOUtils::DMAAlloc(
	    sizeof(usb_setup_packet), &cmd_phys_addr);
	cmd->bmRequestType = 0;
	cmd->bRequest = USB_SETUP_PACKET_SET_ADDRESS;
	cmd->wValue = address;
	cmd->wIndex = 0;
	cmd->wLength = 0;
	log(mod, "cmd : 0x%x", cmd_phys_addr);
	send_async_with_response(0, 0, (uint32_t)cmd_phys_addr,
	                         sizeof(usb_setup_packet), 0, 0);
	IOUtils::sleep(2);
}

void EHCIModule::usb_get_descriptor(uint8_t addr, uint8_t type, uint8_t index,
                                    uint8_t len, uint8_t* data) {
	size_t total_size = sizeof(struct usb_setup_packet) + len;
	uintptr_t base_phys_addr = 0;
	uint8_t* base_vaddr =
	    (uint8_t*)IOUtils::DMAAlloc(total_size, &base_phys_addr);

	auto* cmd = (struct usb_setup_packet*)base_vaddr;
	cmd->bRequest = 0x06;
	cmd->bmRequestType = 0x80;
	cmd->wValue = (type << 8) | index;
	cmd->wIndex = 0;
	cmd->wLength = len;

	uint8_t* data_buf = base_vaddr + sizeof(usb_setup_packet);
	uintptr_t data_phys = base_phys_addr + sizeof(usb_setup_packet);

	send_async_with_response(addr, 0, (uint32_t)base_phys_addr,
	                         sizeof(usb_setup_packet), data_phys, len);

	IOUtils::memcpy((void*)data, (void*)data_buf, len);
	IOUtils::DMAFree((void*)base_phys_addr, (void*)base_vaddr, total_size);
}

void EHCIModule::set_controller(ioforge_usb_controller_service* controller) {
	this->controller = controller;
}

/* QH/QTD cache */
boolean_t EHCIModule::retrieve_qh(ehci_queue_head_node_t** out) {
	size_t h, t;
	do {
		h = __atomic_load_n(&qh_ring_head, __ATOMIC_ACQUIRE);
		t = __atomic_load_n(&qh_ring_tail, __ATOMIC_ACQUIRE);
		if (h == t)
			return false;
	} while (!__atomic_compare_exchange_n(&qh_ring_head, &h, h + 1, false,
	                                      __ATOMIC_ACQ_REL,
	                                      __ATOMIC_ACQUIRE));
	uint16_t idx = qh_ring[h & EHCI_MAX_QH_CACHE_MASK];
	*out = &qh_pool[idx];
	return true;
}

boolean_t EHCIModule::retrieve_qtd(ehci_queue_task_descriptor_node_t** out) {
	size_t h, t;
	do {
		h = __atomic_load_n(&qtd_ring_head, __ATOMIC_ACQUIRE);
		t = __atomic_load_n(&qtd_ring_tail, __ATOMIC_ACQUIRE);
		if (h == t)
			return false;
	} while (!__atomic_compare_exchange_n(&qtd_ring_head, &h, h + 1, false,
	                                      __ATOMIC_ACQ_REL,
	                                      __ATOMIC_ACQUIRE));
	uint16_t idx = qtd_ring[h & EHCI_MAX_QTD_CACHE_MASK];
	*out = &qtd_pool[idx];
	return true;
}

void EHCIModule::store_qh(ehci_queue_head_node_t** in) {
	if (!in || !*in)
		return;
	ioforge_memset((*in)->head, 0, sizeof(ehci_queue_head));
	uint16_t idx = (uint16_t)((*in) - &qh_pool[0]);
	size_t t, h;
	do {
		t = __atomic_load_n(&qh_ring_tail, __ATOMIC_ACQUIRE);
		h = __atomic_load_n(&qh_ring_head, __ATOMIC_ACQUIRE);
		if (t - h >= EHCI_MAX_QH_CACHE) {
			*in = 0;
			return;
		}
	} while (!__atomic_compare_exchange_n(&qh_ring_tail, &t, t + 1, false,
	                                      __ATOMIC_ACQ_REL,
	                                      __ATOMIC_ACQUIRE));
	qh_ring[t & EHCI_MAX_QH_CACHE_MASK] = idx;
	*in = 0;
}

void EHCIModule::store_qtd(ehci_queue_task_descriptor_node_t** in) {
	if (!in || !*in)
		return;
	ioforge_memset((*in)->task_descriptor, 0,
	               sizeof(ehci_queue_task_descriptor));
	uint16_t idx = (uint16_t)((*in) - &qtd_pool[0]);
	size_t t, h;
	do {
		t = __atomic_load_n(&qtd_ring_tail, __ATOMIC_ACQUIRE);
		h = __atomic_load_n(&qtd_ring_head, __ATOMIC_ACQUIRE);
		if (t - h >= EHCI_MAX_QTD_CACHE) {
			*in = 0;
			return;
		}
	} while (!__atomic_compare_exchange_n(&qtd_ring_tail, &t, t + 1, false,
	                                      __ATOMIC_ACQ_REL,
	                                      __ATOMIC_ACQUIRE));
	qtd_ring[t & EHCI_MAX_QTD_CACHE_MASK] = idx;
	*in = 0;
}

void EHCIModule::push_to_qh(ehci_queue_head_node_t* qh_node) {
	spin_acquire(&schedule_lock);
	auto* mq = main_async_qh->head;
	uint32_t saved_next = mq->qhlp;
	qh_node->head->qhlp = saved_next;
	__sync_synchronize();
	mq->qhlp = (uint32_t)qh_node->physaddr | EHCI_Q_SELECT_QH;
	spin_release(&schedule_lock);
}