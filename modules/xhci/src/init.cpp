#include "ioforge/ioforge.h"
#include "xhci/xhci.hpp"
#include <ioforge/ioforge.hpp>
#include <ioforge/ioforge_pci.h>
#include <ioforge/ioforge_pci.hpp>
#include <ioforge/ioforge_usb.h>

XHCIModule XHCIModule::instance;

extern "C" void load() { XHCIModule::getInstance()->load(); }

XHCIModule::XHCIModule() : IOforgePCI("XHCI") {}

XHCIModule* XHCIModule::getInstance() { return &instance; }

extern "C" void xhci_fire_handler();

void XHCIModule::load() {
	device = findDevice(XHCI_VENDOR_ID, XHCI_DEVICE_ID);
	if (!device) {
		log(mod, "Device not found (0x%x:0x%x)", XHCI_VENDOR_ID,
		    XHCI_DEVICE_ID);
		return;
	}
	log(mod, "XHCI device found at %x:%x.%d", device->bus, device->device,
	    device->function);

	uintptr_t bar = device->bar[0].address;
	log(mod, "BAR0: 0x%x", bar);

	cap_regs = (struct xhci_cap_regs*)bar;
	op_regs = (struct xhci_op_regs*)(bar + cap_regs->caplength);
	runtime_regs = (struct xhci_runtime_regs*)(bar + cap_regs->rtsoff);
	doorbell_regs = (volatile uint32_t*)(bar + cap_regs->dboff);

	reset_controller();
	init_controller();

	uint16_t msix_cap = pci_cap_find_msix(device);
	uint16_t msi_cap = pci_cap_find_msi(device);

	if (msix_cap) {
		log(mod, "MSI-X Available at cap : 0x%x", msix_cap);
		auto cpu = ioforge_get_current_core_id();
		auto irq = IOUtils::irq_alloc_entry();
		IOUtils::irq_register(irq, (void*)xhci_fire_handler);
		pci_enable_msix(device, irq, cpu, msix_cap);
	} else if (msi_cap) {
		log(mod, "MSI Available at 0x%x", msi_cap);
		auto cpu = ioforge_get_current_core_id();
		auto irq = IOUtils::irq_alloc_entry();
		IOUtils::irq_register(irq, (void*)xhci_fire_handler);
		pci_enable_msi(device, irq, cpu, msi_cap);
	} else if (device->interrupt_line) {
		auto vector = isr_irq_register(device->interrupt_line,
		                               (void*)xhci_fire_handler);
		// Note: no controller->irq = vector here as XHCI doesn't store it yet
	}

	probe_ports();

	log(mod, "XHCI Loaded");
}

extern "C" void xhci_send_async_stub(uint32_t addr, uint8_t endpoint,
                                     uintptr_t data_phys, size_t request_size,
                                     uintptr_t response_phys,
                                     size_t response_size) {
	// data_phys is a physical address to the 8-byte setup packet.
	// We must map it to read the actual data.
	uintptr_t vaddr = IOforgeMMapPhys(data_phys, 8);
	uint64_t setup_data = *(uint64_t*)vaddr;
	IOforgeMUnmapPhys(vaddr, 8);

	XHCIModule::getInstance()->send_async_with_response(
	    addr, endpoint, setup_data, request_size, response_phys,
	    response_size);
}

__attribute__((constructor)) static void xhci_constructor() {
	struct ioforge_usb_controller_service* usb_controller =
	    (struct ioforge_usb_controller_service*)IOForge::IOUtils::alloc(
	        sizeof(struct ioforge_usb_controller_service));
	usb_controller->service.type = IOFORGE_USB_CONTROLLER;

	IOForge::IOUtils::strcopy((char*)usb_controller->service.name,
	                          (char*)"XHCI");
	usb_controller->ops.send = xhci_send_async_stub;

	XHCIModule::getInstance()->set_controller(usb_controller);
	ioforge_attach(ioforge_get_usb_ctrl_root(), &usb_controller->service);
}
