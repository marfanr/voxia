#include "xhci/xhci.hpp"
#include "ioforge/ioforge.h"
#include <ioforge/ioforge_usb.h>
#include <ioforge/ioforge.hpp>

IoForgeModuleConstructor(XHCIModule);

XHCIModule XHCIModule::instance;

XHCIModule::XHCIModule() : IOforgePCI("XHCI") {
}

XHCIModule* XHCIModule::getInstance() {
	return &instance;
}

void XHCIModule::load() {
	device = findDevice(XHCI_VENDOR_ID, XHCI_DEVICE_ID);
	if (!device) {
		log(mod, "Device not found (0x%x:0x%x)", XHCI_VENDOR_ID,
		    XHCI_DEVICE_ID);
		// Try searching for any XHCI controller (Class 0C, Subclass 03, ProgIf 30)
		// But findDevice only takes vendor/device ID in this framework?
		// Let's assume the user wants me to handle the one I found.
		return;
	}
	log(mod, "XHCI device found at %x:%x.%d", device->bus, device->device,
	    device->function);

	uintptr_t bar = device->bar[0].address;
	log(mod, "BAR0: 0x%x", bar);

	cap_regs = (struct xhci_cap_regs*) bar;
	op_regs = (struct xhci_op_regs*) (bar + cap_regs->caplength);
	runtime_regs = (struct xhci_runtime_regs*) (bar + cap_regs->rtsoff);
	doorbell_regs = (volatile uint32_t*) (bar + cap_regs->dboff);

	log(mod, "HCI version: %x", cap_regs->hciversion);

	reset_controller();
	init_controller();

	// Enable interrupts if needed
	if (device->interrupt_line) {
		auto irq = IOUtils::irq_alloc_entry();
		IOUtils::isr_map(device->interrupt_line, irq);
		IOUtils::irq_register(irq, (void*) XHCIModule::fireHandler);
	}

	probe_ports();

	log(mod, "XHCI Loaded");
}

extern "C" void
xhci_send_async_stub(uint32_t addr, uint8_t endpoint, uint32_t data_phys,
		     size_t request_size, uint32_t response_phys,
		     size_t response_size) {
	XHCIModule::getInstance()->send_async_with_response(
		addr, endpoint, data_phys, request_size, response_phys,
		response_size);
}

__attribute__((constructor)) static void xhci_constructor() {
	struct ioforge_usb_controller_device* usb_controller =
		(struct ioforge_usb_controller_device*) IOForge::IOUtils::alloc(
			sizeof(struct ioforge_usb_controller_device));
	usb_controller->base.type = IOFORGE_USB_CONTROLLER;

	IOForge::IOUtils::strcopy((char*) usb_controller->base.name,
				  (char*) "XHCI");
	usb_controller->ops.send = xhci_send_async_stub;

	XHCIModule::getInstance()->set_controller(usb_controller);
	ioforge_attach(ioforge_get_usb_ctrl_root(), &usb_controller->base);
}

void XHCIModule::fireHandler() {
	// Handle interrupts
}
