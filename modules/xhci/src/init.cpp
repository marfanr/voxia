#include "ioforge/ioforge.h"
#include "memory/kalloc.h"
#include "usb.h"
#include "xhci/xhci.hpp"
#include <ioforge/ioforge.hpp>
#include <ioforge/ioforge_pci.h>
#include <ioforge/ioforge_pci.hpp>
#include <ioforge/ioforge_usb.h>

XHCIModule XHCIModule::instance;

extern "C" void load() { XHCIModule::getInstance()->load(); }

XHCIModule::XHCIModule()
    : IOforgePCI("XHCI"), pending_hotplug_bitmap(0),
      next_interrupter_target(0) {}

XHCIModule* XHCIModule::getInstance() { return &instance; }


// TODO: refactor properly
extern "C" void xhci_fire_handler();
extern "C" void xhci_fire_handler_0();
extern "C" void xhci_fire_handler_1();
extern "C" void xhci_fire_handler_2();
extern "C" void xhci_fire_handler_3();

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
		uint8_t core_count = IOUtils::get_active_core_count();

		void (*handlers[])() = {
		    xhci_fire_handler_0, xhci_fire_handler_1,
		    xhci_fire_handler_2, xhci_fire_handler_3};

		uint32_t max_i = XHCIModule::getInstance()->get_max_intrs();
		if (max_i > 4)
			max_i = 4;

		log(mod, "Distributing %d interrupts across %d cores", max_i,
		    core_count);

		uintptr_t msix_table = 0;
		volatile uint32_t* table = nullptr;

		for (uint32_t i = 0; i < max_i; i++) {
			uint8_t target_core = i % core_count;
			auto irq = IOUtils::irq_alloc_on_core(target_core);
			IOUtils::irq_register_on_core(target_core, irq,
			                              (void*)handlers[i]);

			if (i == 0) {
				msix_table = pci_enable_msix(
				    device, irq, target_core, msix_cap);
				if (!msix_table)
					break;
				table = (volatile uint32_t*)msix_table;
			} else {
				table[i * 4 + 0] =
				    0xFEE00000 |
				    (uint32_t)(target_core
				               << 12); // Address Low
				table[i * 4 + 1] = 0;  // Address High
				table[i * 4 + 2] =
				    (irq & 0xFF);     // Data (Vector)
				table[i * 4 + 3] = 0; // Unmask
			}
		}

		if (msix_table) {
			log(mod, "MSI-X configured with %d interrupters",
			    max_i);
		}
	} else if (msi_cap) {
		log(mod, "MSI Available at 0x%x", msi_cap);
		auto cpu = ioforge_get_current_core_id();
		auto irq = IOUtils::irq_alloc_entry();
		IOUtils::irq_register(irq, (void*)xhci_fire_handler);
		pci_enable_msi(device, irq, cpu, msi_cap);
	} else if (device->interrupt_line) {
		isr_irq_register(device->interrupt_line,
		                 (void*)xhci_fire_handler);
	}

	probe_ports();

	log(mod, "XHCI Loaded");
}

extern "C" void xhci_send_async_stub(uint32_t addr, uint8_t endpoint,
                                     uintptr_t data_phys, void* data_virt, size_t request_size,
                                     uintptr_t response_phys,
                                     size_t response_size) {

	uint64_t setup_data = data_phys;
	if (request_size == sizeof(struct usb_setup_packet) && data_virt != nullptr) {
		setup_data = *(uint64_t*)data_virt;
	}

	XHCIModule::getInstance()->send_async_with_response(
	    addr, endpoint, setup_data, request_size, response_phys,
	    response_size);
}

__attribute__((constructor)) static void xhci_constructor() {
	struct ioforge_usb_controller_service* usb_controller =
	    (struct ioforge_usb_controller_service*)kalloc(
	        sizeof(struct ioforge_usb_controller_service));
	usb_controller->service.type = IOFORGE_USB_CONTROLLER;

	IOForge::IOUtils::strcopy((char*)usb_controller->service.name,
	                          (char*)"XHCI");
	usb_controller->ops.send = xhci_send_async_stub;

	XHCIModule::getInstance()->set_controller(usb_controller);
	ioforge_attach(ioforge_get_usb_ctrl_root(), &usb_controller->service);
}
