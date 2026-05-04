#include "ehci/ehci.hpp"

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

struct usb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
};

struct usb_config_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
};

struct usb_interface {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
};

struct usb_endpoint_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
};

struct usb_string_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wData[];
};

#define HCSPARAM_N_PORTS_MASK 0b1111

void EHCIModule::probe() {

	int ports = *hcsparam & HCSPARAM_N_PORTS_MASK;
	log(mod, "EHCI: port available : %d ", ports);
	for (int i = 0; i < ports; i++) {
		port_reset(i);

		boolean_t available = ehci_op->portsc[i] & EHCI_PORT_ENABLED;

		if (available) {
			log(mod, "Port %d Available", i);
			assign_address(i + 1);

			uint8_t* data = (uint8_t*) IOUtils::alloc(0x1000);
			usb_get_descriptor(i + 1, 1, 0,
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

			uint8_t devClass = dev->bDeviceClass;
			uint8_t devSubClass = dev->bDeviceSubClass;
			uint8_t devProtocol = dev->bDeviceProtocol;

			usb_get_descriptor(i + 1, 2, 0,
					   sizeof(usb_config_descriptor), data);
			usb_config_descriptor* config =
				(usb_config_descriptor*) data;
			usb_get_descriptor(i + 1, 2, 0, config->wTotalLength,
					   data);
			config = (usb_config_descriptor*) data;

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

			struct usb_interface* interface =
				(struct usb_interface*) ((uintptr_t) config
							 + config->bLength);
			log(mod, " Interface length : %d", interface->bLength);
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

			if (devClass == 0 && devSubClass == 0) {
				devClass = interface->bInterfaceClass;
				devSubClass = interface->bInterfaceSubClass;
				devProtocol = interface->bInterfaceProtocol;
			}

			if (config->iConfiguration > 0) {
				size_t iconf_len =
					config->bLength
					- (sizeof(struct usb_config_descriptor)
					   + sizeof(struct usb_interface)
					   + interface->bNumEndpoints
						     * sizeof(
							     struct
							     usb_endpoint_descriptor));
				usb_get_descriptor(i + 1, 3,
						   config->iConfiguration,
						   iconf_len, data);
				struct usb_string_descriptor* str =
					(struct usb_string_descriptor*) data;
				log(mod, "string descriptor length : %d",
				    str->bLength);
				log(mod, "string descriptor type : %d",
				    str->bDescriptorType);
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
					i + 1, (uint32_t) setup_paddr,
					sizeof(usb_setup_packet), in_data_paddr,
					32);
				log(mod, "Report Descriptor : ");
				for (int i = 0; i < 32; i++) {
					serial2_printf("0x%x ", in_data[i]);
				}
				serial2_printf("\n");
			}

			// get descirptor
			// ini harusnya di hid
			if (devClass == 0x3) {
				serial2_printf("HID Device\n");
				if (devProtocol == 1) {
					serial2_printf(
						"HID Device : Keyboard\n");
				} else if (devProtocol == 2) {
					serial2_printf("HID Device : Mouse\n");
				}
			}
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
	setup->token |= (0x2 << 8);		      // type is setup
	setup->token |= (0x3 << 10);		      // maxerror
	setup->buffer[0] = (uint32_t) data_phys;

	data->link = (uint32_t) status_phys_addr;
	data->altlink = EHCI_QTD_TERMINATE;
	data->token = (response_size << 16); // setup size
	data->token |= (1 << 7);	     // aktif
	data->token |= (1 << 31);	     // toggle
	data->token |= (0x1 << 8);	     // type is in
	data->token |= (0x3 << 10);	     // maxerror
	data->buffer[0] = (uint32_t) (uintptr_t) response;

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
	head2->ch |= addr;
	head2->cap = EHCI_QH_CAP_MULT_1;

	head->qhlp = (uint32_t) head2_phys_addr | EHCI_Q_SELECT_QH;
	head->altTD = EHCI_QTD_TERMINATE;
	head->nextTD = EHCI_QTD_TERMINATE;
	head->currentTD = 0;
	head->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION | 0;

	ehci_op->asynclistaddr = (uint32_t) (uintptr_t) head2_phys_addr;

	is_trasaction_is_running = 1;
	procces_async(status);
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
			log(mod, "Success");
			// status len
			size_t len = qtd->token & 0xffff;
			log(mod, "len : %d", len);
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
	log(mod, "cmd : 0x%x", cmd_phys_addr);

	size_t aligned_size = (len + 0x1000 - 1) / 0x1000;
	uintptr_t data_phys_addr = 0;
	uint8_t* data_ = (uint8_t*) IOUtils::DMAAlloc(aligned_size * 0x1000,
						      &data_phys_addr);
	log(mod, "data : 0x%x", data_phys_addr);
	send_async_with_response(addr, (uint32_t) cmd_phys_addr,
				 sizeof(usb_setup_packet), data_phys_addr,
				 sizeof(usb_device_descriptor));

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

void EHCIModule::set_controller(USBController* controller) {
	this->controller = controller;
}