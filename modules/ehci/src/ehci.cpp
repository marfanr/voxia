#include "type.h"
#include "usb.h"
#include "ehci/ehci.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "ehci/ehci_pipe.hpp"
#include "ioforge/ioforge_usb.h"
#include <ioforge/ioforge_new.hpp>

#define EHCI_MAX_QH_CACHE 4096
#define EHCI_MAX_QH_CACHE_MASK (EHCI_MAX_QH_CACHE - 1)
#define EHCI_MAX_QTD_CACHE 4096
#define EHCI_MAX_QTD_CACHE_MASK (EHCI_MAX_QTD_CACHE - 1)

// cache
static ehci_queue_head_node_t qh_pool[EHCI_MAX_QH_CACHE];
static uint16_t qh_ring[EHCI_MAX_QH_CACHE]; // isi = index ke pool
static size_t qh_ring_head = 0;
static size_t qh_ring_tail = 0;

static ehci_queue_task_descriptor_node_t qtd_pool[EHCI_MAX_QTD_CACHE];
static uint16_t qtd_ring[EHCI_MAX_QTD_CACHE]; // isi = index ke pool
static size_t qtd_ring_head = 0;
static size_t qtd_ring_tail = 0;

// for periodic
static ehci_queue_head_node_t* framelist_node[1024];

// used qh
static ehci_queue_head_node_t* main_async_qh = 0;

void EHCIModule::init_controller() {

	static_assert(sizeof(ehci_queue_head) % 32 == 0,
		      "ehci_queue_head must be 32-byte aligned");
	static_assert(sizeof(ehci_queue_task_descriptor) % 32 == 0,
		      "ehci_queue_task_descriptor must be 32-byte aligned");

	size_t alloc_count = 0;
	// init qh
	{
		size_t ehci_qh_block_per_alloc =
			0x1000 / sizeof(ehci_queue_head);

		for (size_t i = 0; i < EHCI_MAX_QH_CACHE;
		     i += ehci_qh_block_per_alloc, alloc_count++) {

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
				qh_pool[i + j].head = qh;
				qh_pool[i + j].physaddr =
					(uint32_t) (physaddr + offset);
				qh_pool[i + j].next = 0;

				qh_ring[i + j] = i + j;
			}
		}
		qh_ring_head = 0;
		qh_ring_tail = EHCI_MAX_QH_CACHE;
	}

	{
		size_t ehci_qtd_block_per_alloc =
			0x1000 / sizeof(ehci_queue_task_descriptor);
		for (size_t i = 0; i < EHCI_MAX_QTD_CACHE;
		     i += ehci_qtd_block_per_alloc, alloc_count++) {

			uintptr_t physaddr = 0;
			uintptr_t vaddr = (uintptr_t) IOUtils::DMAAlloc(
				0x1000, &physaddr);

			if (!vaddr)
				break;

			ioforge_memset((void*) vaddr, 0, 0x1000);

			for (size_t j = 0; j < ehci_qtd_block_per_alloc; j++) {
				if ((i + j) >= EHCI_MAX_QTD_CACHE)
					break;

				uint32_t offset =
					j * sizeof(ehci_queue_task_descriptor);
				struct ehci_queue_task_descriptor* qtd =
					(struct
					 ehci_queue_task_descriptor*) (vaddr
								       + offset);

				qtd_pool[i + j].physaddr =
					(uint32_t) (physaddr + offset);
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

	// verifikasi allignment
	// Cek node pertama dan kedua — kalau selisihnya bukan 32 kelipatan, struct salah
	{
		uint32_t p0 = qh_pool[0].physaddr;
		uint32_t p1 = qh_pool[1].physaddr;
		if ((p1 - p0) % 32 != 0) {
			log(mod,
			    "ERROR: QH alignment salah! sizeof=%d, diff=%d",
			    sizeof(ehci_queue_head), p1 - p0);
			return;
		}

		uint32_t q0 = qtd_pool[0].physaddr;
		uint32_t q1 = qtd_pool[1].physaddr;
		if ((q1 - q0) % 32 != 0) {
			log(mod,
			    "ERROR: qTD alignment salah! sizeof=%d, diff=%d",
			    sizeof(ehci_queue_task_descriptor), q1 - q0);
			return;
		}

		log(mod, "alignment OK — QH stride=%d, qTD stride=%d", p1 - p0,
		    q1 - q0);
	}

	// init first queue head
	ehci_queue_head_node_t* qh_node = 0;
	if (!retrieve_qh(&qh_node))
		return;

	struct ehci_queue_head* qh = qh_node->head;
	qh->qhlp = qh_node->physaddr | EHCI_Q_SELECT_QH;
	qh->altTD = EHCI_QTD_TERMINATE;
	qh->nextTD = EHCI_QTD_TERMINATE;
	qh->currentTD = 0;
	qh->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION | 0;

	log(mod, "first qh: 0x%x", qh);
	main_async_qh = qh_node;
	ehci_op->asynclistaddr = (uint32_t) (uintptr_t) qh_node->physaddr;
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
	while ((ehci_op->usbsts & EHCI_HC_HALTED_STATUS))
		;
	log(mod, "EHCI: start engine");
}

void EHCIModule::init_periodic() {
	log(mod, "periodic: init_que_head");

	// init framelist
	uintptr_t framelist_phys_addr = 0;
	framelist = (uint32_t*) IOUtils::DMAAlloc(1024 * sizeof(uint32_t),
						  &framelist_phys_addr);
	IOUtils::memset(framelist, 0, 1024 * sizeof(uint32_t));

	ehci_queue_head_node_t* qh_node = 0;
	retrieve_qh(&qh_node);
	struct ehci_queue_head* qh = qh_node->head;
	IOUtils::memset(qh, 0, sizeof(*qh));
	qh->qhlp = qh_node->physaddr | EHCI_Q_SELECT_QH;
	qh->altTD = EHCI_QTD_TERMINATE;
	qh->nextTD = EHCI_QTD_TERMINATE;
	qh->currentTD = 0;
	qh->token = 0;
	qh->ch = EHCI_QH_CAP_HEAD_OF_RECLAMATION;
	qh->cap = 0xFF;

	for (int i = 0; i < 1024; i++) {
		framelist[i] = ((uint32_t) (uintptr_t) qh_node->physaddr)
			       | EHCI_Q_SELECT_QH;
		framelist_node[i] = qh_node;
	}

	log(mod, "framelist : 0x%x (0x%x)", framelist, framelist_phys_addr);
	ehci_op->frindex = 0;
	ehci_op->periodiclistbase = (uint32_t) (uintptr_t) framelist_phys_addr;

	start_periodic();
}

void EHCIModule::arm_periodic_qtd(struct ehci_periodic_transfer_ctx* ctx) {
	struct ehci_queue_task_descriptor* data_qtd =
		ctx->data_node->task_descriptor;
	struct ehci_queue_head* qh = ctx->qh_node->head;

	// Reset qTD
	data_qtd->link = EHCI_QTD_TERMINATE;
	data_qtd->altlink = EHCI_QTD_TERMINATE;
	data_qtd->token = EHCI_QTD_TOKEN_LENGTH(ctx->response_size)
			  | EHCI_QTD_TOKEN_STATUS_ACTIVE
			  | ctx->toggle // DATA0 atau DATA1
			  | EHCI_QTD_TOKEN_PID_IN | EHCI_QTD_TOKEN_ERROR_COUNT_3
			  | EHCI_QTD_TOKEN_IOC;
	data_qtd->buffer[0] = ctx->response_buf;

	// Toggle untuk transfer berikutnya
	ctx->toggle ^= EHCI_QTD_TOKEN_DATA;

	// Re-link ke QH — pastikan overlay token di QH bersih dulu
	qh->token = 0;
	__sync_synchronize();
	qh->nextTD = ctx->data_node->physaddr;
}

struct ehci_periodic_transfer_ctx* test_ctx = 0;

void EHCIModule::get_data_periodic(uint8_t addr, uint16_t ring,
				   uint8_t endpoint, uint32_t response,
				   size_t response_size) {

	test_ctx = (struct ehci_periodic_transfer_ctx*) IOUtils::alloc(
		sizeof(struct ehci_periodic_transfer_ctx));

	uintptr_t data_paadr = 0;
	auto data = (uintptr_t) IOUtils::DMAAlloc(4096, &data_paadr);
	IOUtils::memset((void*) data, 0, 4096);

	test_ctx->response_buf = data_paadr;
	test_ctx->response_size = response_size;
	test_ctx->response = data;
	test_ctx->toggle = 0;

	// Alokasi QH
	retrieve_qh(&test_ctx->qh_node);
	struct ehci_queue_head* qh = test_ctx->qh_node->head;
	IOUtils::memset(qh, 0, sizeof(struct ehci_queue_head));

	qh->altTD = EHCI_QTD_TERMINATE;
	qh->currentTD = 0;
	qh->ch = (addr & 0x7F);
	qh->ch |= ((endpoint & 0xF) << 8);
	qh->ch |= (2 & 0x3) << 12;		 // EPS High Speed
	qh->ch |= (response_size & 0x7FF) << 16; // Max Packet
	qh->cap = (1 << 0);			 // S-mask
	qh->cap |= EHCI_QH_CAP_MULT_1;		 // ← FIX 2

	// qtd
	retrieve_qtd(&test_ctx->data_node);
	IOUtils::memset(test_ctx->data_node->task_descriptor, 0,
			sizeof(struct ehci_queue_task_descriptor));

	arm_periodic_qtd(test_ctx);

	// putting into periodic
	auto curr_node = framelist_node[ring];
	struct ehci_queue_head* mq = curr_node->head;
	uint32_t saved_next = mq->qhlp;
	qh->qhlp = saved_next;
	__sync_synchronize();
	mq->qhlp = (uint32_t) test_ctx->qh_node->physaddr | EHCI_Q_SELECT_QH;

	// JANGAN print di sini — data belum ada, baca di fireHandler
	log(mod, "periodic transfer armed for addr=%d ep=%d", addr, endpoint);
}

void EHCIModule::insert_periodic(ehci_queue_head_node_t* qh_node,
				 uint16_t interval_ms) {
	// TODO: handle interval ms
	// Cari slot di framelist yang sesuai dengan interval
	int ring = 0;

	auto curr_node = framelist_node[ring];
	struct ehci_queue_head* mq = curr_node->head;
	uint32_t saved_next = mq->qhlp;
	qh_node->head->qhlp = saved_next;
	__sync_synchronize();
	mq->qhlp = (uint32_t) qh_node->physaddr | EHCI_Q_SELECT_QH;

	log(mod, "QH dengan interval %d ms dimasukkan ke slot %d", interval_ms,
	    ring);
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
	// TODO: buat fungsi untuk umount device

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
			assign_address(addr);
			log(mod, "Port %d Available", i);

			// save ke ioforge
			struct ioforge_usb_device* usbDevice =
				(struct ioforge_usb_device*) IOUtils::alloc(
					sizeof(struct ioforge_usb_device));
			IOUtils::memset(usbDevice, 0, sizeof(*usbDevice));

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
			char iManufacturer[64] = {0};
			usb_get_string_descriptor(addr, dev->iManufacturer,
						  iManufacturer,
						  sizeof(iManufacturer));
			log(mod, "USB Manufacturer: %s", iManufacturer);
			char iProduct[64] = {0};
			usb_get_string_descriptor(addr, dev->iProduct, iProduct,
						  sizeof(iProduct));
			log(mod, "USB Product: %s", iProduct);
			char iSerialNumber[64] = {0};
			if (dev->iSerialNumber != 0) {
				usb_get_string_descriptor(
					addr, dev->iSerialNumber, iSerialNumber,
					sizeof(iSerialNumber));
				log(mod, "USB Serial Number: %s",
				    iSerialNumber);
			} else {
				// Jika tidak ada serial number, berikan nilai default atau biarkan kosong
				log(mod, "USB Serial Number: [Not Provided]");
				IOUtils::strcopy(iSerialNumber, (char*) "None");
			}
			IOUtils::strcopy((char*) usbDevice->serial_number,
					 iSerialNumber);

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
				log(mod, "USB Configuration Name: %s\n",
				    iConfiguration);

				// ioforge
				usbDevice->max_power = config->bMaxPower;
				//

				// interface
				struct usb_interface* interface =
					(struct
					 usb_interface*) ((uintptr_t) config
							  + config->bLength);
				log(mod, "[Interface] Interface length : %d",
				    interface->bLength);
				log(mod, "[Interface] Interface number : %d",
				    interface->bInterfaceNumber);
				log(mod, "[Interface] Interface type : %d",
				    interface->bDescriptorType);
				log(mod, "[Interface] Interface class : %d",
				    interface->bInterfaceClass);
				log(mod,
				    "[Interface]  USB Device sub class : %d ",
				    interface->bInterfaceSubClass);
				log(mod, "[Interface] Protocol : %d",
				    interface->bInterfaceProtocol);
				log(mod, "[Interface] number endpoint : %d",
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
				uint8_t* ptr = (uint8_t*)((uintptr_t)interface + interface->bLength);

				for (int k = 0; k < interface->bNumEndpoints;
				     k++) {

					while (ptr
					       < (uint8_t*) data
							 + config->wTotalLength) {
						uint8_t desc_len = ptr[0];
						uint8_t desc_type = ptr[1];
						if (desc_type == 0x05)
							break; // Endpoint descriptor
						if (desc_len == 0)
							break; // Safety: hindari infinite loop
						ptr += desc_len;
					}

					struct usb_endpoint_descriptor* endpoint =
						(struct
						 usb_endpoint_descriptor*) ptr;

					log(mod, " [Endpoint %d] length : %d",
					    k, endpoint->bLength);
					log(mod, " [Endpoint %d] type : %d", k,
					    endpoint->bDescriptorType);
					log(mod,
					    " [Endpoint %d] address : 0x%x", k,
					    endpoint->bEndpointAddress);
					log(mod,
					    " [Endpoint %d] attributes : 0x%x",
					    k, endpoint->bmAttributes);
					log(mod, " [Endpoint %d] interval : %d",
					    k, endpoint->bInterval);

					auto& current_endpoint =
						usbDevice->endpoints
							[endpoint_count];
					current_endpoint.address =
						endpoint->bEndpointAddress;
					current_endpoint.attributes =
						endpoint->bmAttributes;
					current_endpoint.interval =
						endpoint->bInterval;
					current_endpoint.max_packet =
						endpoint->wMaxPacketSize;

					endpoint =
						(struct
						 usb_endpoint_descriptor*) ((uintptr_t)
										    endpoint
									    + endpoint->bLength);
				}

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
					addr, 0, (uint32_t) setup_paddr,
					sizeof(usb_setup_packet), in_data_paddr,
					32);

				log(mod, "Report Descriptor : ");
				for (int i = 0; i < 32; i++) {
					serial2_printf("0x%x ", in_data[i]);
				}
				serial2_printf("\n");

				IOUtils::DMAFree((void*) in_data_paddr, in_data,
						 4096);
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
						(char*) usbDevice->base.name,
						(char*) "Keyboard");
					serial2_printf(
						"HID Device : Keyboard\n");
				} else if (dev_protocol == 2) {
					IOUtils::strcopy(
						(char*) usbDevice->base.name,
						(char*) "Mouse");
					serial2_printf("HID Device : Mouse\n");
				}
			}

			usbDevice->base.type = IOFORGE_USB_DEVICE;

			// TODO: handle free ini kalau dev nya di umount
			void* mem = IOForge::IOUtils::alloc(sizeof(EHCIPipe));
			EHCIPipe* pipe = new (mem) EHCIPipe(this);

			usbDevice->pipe = pipe;

			ioforge_attach(ioforge_get_usb_devices_root(),
				       &usbDevice->base);
		}
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

	// Alokasi QH
	ehci_queue_head_node_t* qh_node;
	retrieve_qh(&qh_node);
	uintptr_t qh_phys = qh_node->physaddr;
	struct ehci_queue_head* qh = qh_node->head;
	IOUtils::memset(qh, 0, sizeof(struct ehci_queue_head));

	// Alokasi qTD setup
	ehci_queue_task_descriptor_node_t* setup_node;
	retrieve_qtd(&setup_node);
	uintptr_t setup_phys = setup_node->physaddr;
	struct ehci_queue_task_descriptor* setup =
		(struct ehci_queue_task_descriptor*)
			setup_node->task_descriptor;
	IOUtils::memset(setup, 0, sizeof(struct ehci_queue_task_descriptor));

	// Alokasi qTD status
	ehci_queue_task_descriptor_node_t* status_node;
	retrieve_qtd(&status_node);
	uintptr_t status_phys = status_node->physaddr;
	struct ehci_queue_task_descriptor* status =
		(struct ehci_queue_task_descriptor*)
			status_node->task_descriptor;
	IOUtils::memset(status, 0, sizeof(struct ehci_queue_task_descriptor));

	// Bangun chain qTD
	ehci_queue_task_descriptor_node_t* data_node = 0;
	if (response && response_size > 0) {

		retrieve_qtd(&data_node);
		uintptr_t data_qtd_phys = data_node->physaddr;
		struct ehci_queue_task_descriptor* data_qtd =
			(struct ehci_queue_task_descriptor*)
				data_node->task_descriptor;
		IOUtils::memset(data_qtd, 0,
				sizeof(struct ehci_queue_task_descriptor));

		// Setup → Data
		setup->link = (uint32_t) data_qtd_phys;
		setup->altlink = EHCI_QTD_TERMINATE;
		setup->token = EHCI_QTD_TOKEN_LENGTH(size);
		setup->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
		setup->token |= EHCI_QTD_TOKEN_PID_SETUP;
		setup->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
		setup->buffer[0] =
			(uint32_t) data_phys; // ← packet setup 8 byte

		// Data IN
		data_qtd->link = (uint32_t) status_phys;
		data_qtd->altlink = (uint32_t) status_phys;
		data_qtd->token = EHCI_QTD_TOKEN_LENGTH(response_size);
		data_qtd->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
		data_qtd->token |= EHCI_QTD_TOKEN_DATA; // DATA1 toggle
		data_qtd->token |= EHCI_QTD_TOKEN_PID_IN;
		data_qtd->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
		data_qtd->buffer[0] = (uint32_t) response;

		// Status OUT (kebalikan data IN)
		status->link = EHCI_QTD_TERMINATE;
		status->altlink = EHCI_QTD_TERMINATE;
		status->token = EHCI_QTD_TOKEN_LENGTH(0);
		status->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
		status->token |= EHCI_QTD_TOKEN_DATA; // DATA1
		status->token |=
			EHCI_QTD_TOKEN_PID_OUT; // OUT karena data stage IN
		status->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
		status->token |= EHCI_QTD_TOKEN_IOC;

	} else {
		// Tidak ada data stage (contoh: SET_ADDRESS)
		// Setup → Status langsung
		setup->link = (uint32_t) status_phys;
		setup->altlink = EHCI_QTD_TERMINATE;
		setup->token = EHCI_QTD_TOKEN_LENGTH(size);
		setup->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
		setup->token |= EHCI_QTD_TOKEN_PID_SETUP;
		setup->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
		setup->buffer[0] = (uint32_t) data_phys;

		// Status IN (no-data control transfer selalu IN)
		status->link = EHCI_QTD_TERMINATE;
		status->altlink = EHCI_QTD_TERMINATE;
		status->token = EHCI_QTD_TOKEN_LENGTH(0);
		status->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
		status->token |= EHCI_QTD_TOKEN_DATA;
		status->token |= EHCI_QTD_TOKEN_PID_IN;
		status->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
		status->token |= EHCI_QTD_TOKEN_IOC;
	}

	qh->altTD = EHCI_QTD_TERMINATE;
	qh->nextTD = (uint32_t) setup_phys;
	qh->currentTD = 0;
	qh->ch = EHCI_QH_CAP_DTC;
	qh->ch |= EHCI_QH_CAP_MAX_PACKET_LENGTH(64);
	qh->ch |= (2 << 12);	 // EPS = High Speed
	qh->ch |= (addr & 0x7f); // device address
	qh->cap = EHCI_QH_CAP_MULT_1;
	qh->cap |= ((endpoint & 0xF) << 8);

	push_to_qh(qh_node);

	// pooling
	bool done = false;
	for (int i = 0; i < 500 && !done; i++) {
		IOUtils::sleep(10);

		uint32_t tok = status->token;

		if (tok & EHCI_QTD_TOKEN_STATUS_ACTIVE)
			continue; // masih jalan

		if (tok & (1 << 6))
			log(mod, "send_async: HALTED");
		else if (tok & (1 << 5))
			log(mod, "send_async: Data Buffer Error");
		else if (tok & (1 << 4))
			log(mod, "send_async: Babble");
		else if (tok & (1 << 3))
			log(mod, "send_async: Transaction Error");

		done = true;
	}

	if (!done)
		log(mod, "send_async: TIMEOUT");

	// ── Detach QH dari schedule ──────────────────────────────────────────
	// Bypass: main_qh langsung nunjuk ke penerus QH yang mau dilepas
	auto mq = main_async_qh->head;
	__sync_synchronize();
	mq->qhlp = qh->qhlp;

	// doorbell
	ehci_op->usbcmd |= (1 << 6);

	// Hardware akan merespons dengan menge-set bit ke-5 (IAA) di USBSTS
	// Beri batas waktu (timeout) agar sistem tidak hang/freeze
	int timeout = 1000;
	while (!(ehci_op->usbsts & (1 << 5)) && timeout > 0) {
		IOUtils::sleep(1);
		timeout--;
	}

	if (timeout == 0) {
		log(mod, "ERROR: Asynchronous Advance Doorbell timeout!");
	}

	ehci_op->usbsts |= (1 << 5);

	store_qh(&qh_node);
	store_qtd(&setup_node);
	store_qtd(&status_node);
	if (data_node)
		store_qtd(&data_node);
}

void EHCIModule::procces_async(ehci_queue_task_descriptor* qtd) {
	ehci_op->usbcmd |= EHCI_START_ASYNC_SCHEDULE;

	for (int i = 0; i < 100; i++) {
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

	// sendAsync((uint32_t) cmd_phys_addr, sizeof(usb_setup_packet));
	send_async_with_response(0, 0, (uint32_t) cmd_phys_addr,
				 sizeof(usb_setup_packet), 0, 0);

	IOUtils::sleep(2);
}

void EHCIModule::usb_get_descriptor(uint8_t addr, uint8_t type, uint8_t index,
				    uint8_t len, uint8_t* data) {

	size_t total_size = sizeof(struct usb_setup_packet) + len;
	uintptr_t base_phys_addr = 0;
	uint8_t* base_vaddr =
		(uint8_t*) IOUtils::DMAAlloc(total_size, &base_phys_addr);

	struct usb_setup_packet* cmd = (struct usb_setup_packet*) base_vaddr;

	cmd->bRequest = 0x06;
	cmd->bmRequestType = 0x80; // recieve
	cmd->wValue = (type << 8) | index;
	cmd->wIndex = 0;
	cmd->wLength = len;

	uint8_t* data_buf = base_vaddr + sizeof(usb_setup_packet);
	uintptr_t data_phys = base_phys_addr + sizeof(usb_setup_packet);

	send_async_with_response(addr, 0, (uint32_t) base_phys_addr,
				 sizeof(usb_setup_packet), data_phys, len);

	IOUtils::memcpy((void*) data, (void*) data_buf, len);
	IOUtils::DMAFree((void*) base_phys_addr, (void*) base_vaddr,
			 total_size);
}

void EHCIModule::call_completion_callback(ioforge_device* dev) {
	if (!dev)
		return;

	if (dev->type == IOFORGE_USB_DEVICE) {

		auto usb = (ioforge_usb_device*) dev;
		if (usb->pipe != nullptr) {
			EHCIPipe* pipe = (EHCIPipe*) usb->pipe;
			if (pipe->data_node_) {
				auto token = pipe->data_node_->task_descriptor
						     ->token;
				if (token & EHCI_QTD_TOKEN_STATUS_ACTIVE)
					goto next;

				bool is_error = false;
				if (token & (1 << 6)) {
					log("EHCI IRQ", "→ HALTED");
					is_error = true;
				}
				if (token & (1 << 5)) {
					log("EHCI IRQ", "→ Data Buffer Error");
					is_error = true;
				}
				if (token & (1 << 3)) {
					log("EHCI IRQ", "→ Transaction Error");
					is_error = true;
				}

				log(mod, "interrupt dari %s", dev->name);
				pipe->on_complete(0, 0, is_error);
			}
			// pipe->qh_node_->physaddr ==
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

	auto status = module->ehci_op->usbsts;
	if (!(status & 0x3f))
		return;

	if (status & (1 << 0)) {
		// USBINT → transfer complete
		log("EHCI IRQ", "transfer complete");
		auto node = ioforge_get_usb_devices_root();
		module->call_completion_callback(node);
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

	uint16_t idx = (uint16_t) ((*in) - &qh_pool[0]); // hitung index

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

	uint16_t idx = (uint16_t) ((*in) - &qtd_pool[0]); // hitung index

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

// hanya software link, tetep perlu atur flag manual
void EHCIModule::push_to_qh(ehci_queue_head_node_t* qh_node) {
	struct ehci_queue_head* mq = main_async_qh->head;
	uint32_t saved_next = mq->qhlp;
	qh_node->head->qhlp = saved_next; // QH baru → (dulu penerus main_qh)
	__sync_synchronize();
	mq->qhlp = (uint32_t) qh_node->physaddr
		   | EHCI_Q_SELECT_QH; // main_qh → QH baru
}
