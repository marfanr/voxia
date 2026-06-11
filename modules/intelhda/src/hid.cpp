#include "ioforge/ioforge.h"
#include "ioforge/ioforge_int_pipe.hpp"
#include "ioforge/ioforge_usb.h"
#include "usb-hid/keyboard.hpp"
#include "usb.h"
#include "usb-hid/hid.hpp"
#include <ioforge/ioforge.hpp>
#include <usb.h>

HIDKeyboard UsbHid::keyboard;

void UsbHid::hid_device_setup(ioforge_usb_device* dev) {
	if (!dev->pipe) {
		log(mod, "ERROR: missing pipe on %s", dev->base.name);
		return;
	}

	set_configuration(dev, 1);

	set_iddle(dev);

	set_protocol(dev, 0, REPORT_PROTOCOL);

	if (dev->protocol == HID_KEYBOARD) {
		keyboard.load(dev);
	}
}

void UsbHid::set_iddle(ioforge_usb_device* dev) {
	uintptr_t setiddle_paddr = 0;
	struct usb_setup_packet* setiddle =
		(struct usb_setup_packet*) IOUtils::DMAAlloc(
			sizeof(struct usb_setup_packet), &setiddle_paddr);

	setiddle->bRequest = SET_IDLE;
	setiddle->bmRequestType = 0b00100001;
	// setiddle->wValue = 0;
	setiddle->wValue = ((uint16_t) dev->endpoints[0].interval);
	setiddle->wIndex = 0;
	setiddle->wLength = 0;

	// todo: modify send method to add endpoint parameter
	dev->controller->ops.send(dev->addr, dev->endpoints[0].address,
	                          setiddle_paddr, setiddle, sizeof(*setiddle), 0, 0);

	IOUtils::DMAFree((void*) setiddle_paddr, (void*) setiddle,
			 sizeof(*setiddle));
}

void UsbHid::set_report(ioforge_usb_device* dev, uint8_t report) {
	uintptr_t setreport_paddr = 0;
	struct usb_setup_packet* setreport =
		(struct usb_setup_packet*) IOUtils::DMAAlloc(
			sizeof(struct usb_setup_packet), &setreport_paddr);

	setreport->bRequest = SET_REPORT;
	setreport->bmRequestType = 0b00100001;
	setreport->wValue = ((uint16_t) dev->endpoints[0].interval);
	setreport->wIndex = 0;
	setreport->wLength = 0;

	dev->controller->ops.send(dev->addr, dev->endpoints[0].address,
				  setreport_paddr, setreport,
				  sizeof(struct usb_setup_packet), 0, 0);

	IOUtils::DMAFree((void*) setreport_paddr, (void*) setreport,
			 sizeof(struct usb_setup_packet));
}

void UsbHid::get_report(ioforge_usb_device* dev) {
	uintptr_t setreport_paddr = 0;
	struct usb_setup_packet* setreport =
		(struct usb_setup_packet*) IOUtils::DMAAlloc(
			sizeof(struct usb_setup_packet), &setreport_paddr);

	setreport->bRequest = GET_REPORT;
	setreport->bmRequestType = 0b10100001;
	setreport->wValue = ((uint16_t) dev->endpoints[0].interval);
	setreport->wIndex = 0;
	setreport->wLength = 0;

	dev->controller->ops.send(dev->addr, dev->endpoints[0].address,
				  setreport_paddr, setreport,
				  sizeof(struct usb_setup_packet), 0, 0);

	IOUtils::DMAFree((void*) setreport_paddr, (void*) setreport,
			 sizeof(struct usb_setup_packet));
}

void UsbHid::set_protocol(ioforge_usb_device* dev, uint8_t interface,
			  uint8_t protocol) {
	uintptr_t setreport_paddr = 0;
	struct usb_setup_packet* setreport =
		(struct usb_setup_packet*) IOUtils::DMAAlloc(
			sizeof(struct usb_setup_packet), &setreport_paddr);

	setreport->bRequest = SET_PROTOCOL;
	setreport->bmRequestType = 0b00100001;
	setreport->wValue = protocol;
	setreport->wIndex = interface;
	setreport->wLength = 0;

	dev->controller->ops.send(dev->addr, dev->endpoints[0].address,
				  setreport_paddr, setreport,
				  sizeof(struct usb_setup_packet), 0, 0);

	IOUtils::DMAFree((void*) setreport_paddr, (void*) setreport,
			 sizeof(struct usb_setup_packet));
}