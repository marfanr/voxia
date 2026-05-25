#include "ehci/ehci.hpp"
#include "ioforge/ioforge.h"
#include <ioforge/ioforge.hpp>
#include <ioforge/ioforge_usb.h>

IoForgeModuleConstructor(EHCIModule);

EHCIModule::EHCIModule() : IOforgePCI("EHCI") {}

EHCIModule* EHCIModule::getInstance() { return &instance; }

void EHCIModule::load() {
	device = findDevice(EHCI_VENDOR_ID, EHCI_DEVICE_ID);
	if (!device) {
		return;
	}
	log(mod, "device found device 0x%x", device);

	uintptr_t bar = device->bar[0].address;
	log(mod, "bar : 0x%x", bar);
	uint8_t cap_length = *(uint8_t*)(bar);
	ehci_op = (struct ehci_operation*)(bar + cap_length);

	stop_device();
	reset_device();

	ehci_op->frindex = 0;
	ehci_op->ctrldssegment = 0;
	ehci_op->usbcmd |= EHCI_1_MICRO_FRAME | (0b00 << 2);
	ehci_op->configflag = 1;

	start_device();
	init_controller();

	hcsparam = (uint32_t*)(bar + 0x4);
	hccparam = (uint32_t*)(bar + 0x8);

	log(mod, "EHCI setup done");

	init_periodic();

	serial2_printf("EHCI interrupt line: %d\n", device->interrupt_line);

	auto curr_vector = IOUtils::isr_get_vector(device->interrupt_line);
	serial2_printf("E1000 interrupt line: %d (%d)\n",
	               device->interrupt_line, curr_vector);

	if (device->interrupt_line) {
		auto vector = isr_irq_register(device->interrupt_line,
		                               (void*)EHCIModule::fireHandler);
		controller->irq = vector;
	}

	// detect all devices
	probe();

	ehci_op->usbsts = 0x3f;
	ehci_op->usbintr = (1 << 0) | // USBINT
	                   (1 << 1) | // USBERRINT
	                   (1 << 2);  // PORT CHANGE

	log(mod, "Loaded Module");
}

extern "C" void send_async_c_wrapper(uint32_t addr, uint8_t endpoint,
                                     uint32_t data_phys, size_t request_size,
                                     uint32_t response_phys,
                                     size_t response_size) {
	instance.send_async_with_response(addr, endpoint, data_phys,
	                                  request_size, response_phys,
	                                  response_size);
}

__attribute__((constructor)) static void ehci_constructor() {
	// registering controller
	struct ioforge_usb_controller_service* usb_controller =
	    (struct ioforge_usb_controller_service*)IOForge::IOUtils::alloc(
	        sizeof(struct ioforge_usb_controller_service));
	usb_controller->service.type = IOFORGE_USB_CONTROLLER;

	usb_controller->ops.send = send_async_c_wrapper;

	IOForge::IOUtils::strcopy((char*)usb_controller->service.name,
	                          (char*)"EHCI");

	usb_controller->ops.send = send_async_c_wrapper;

	instance.set_controller(usb_controller);

	ioforge_attach(ioforge_get_usb_ctrl_root(), &usb_controller->service);
}
