#include "ehci/ehci.hpp"
#include <ioforge/ioforge.hpp>

typedef struct {
	void (*send)(uint32_t data_phys, size_t size);
} UsbControllerOp;

typedef struct {
	const char* name;
	UsbControllerOp* ops;
} USBController;

extern "C" void ioforge_register_usb_controller(USBController* c);

IoForgeModuleConstructor(EHCIModule);

EHCIModule::EHCIModule() : IOforgePCI("EHCI") {
}

EHCIModule* EHCIModule::getInstance() {
	return &instance;
}

void EHCIModule::load() {
	device = findDevice(EHCI_VENDOR_ID, EHCI_DEVICE_ID);
	if (!device) {
		return;
	}
	log(mod, "device found device 0x%x", device);

	uintptr_t bar = device->bar[0].address;
	log(mod, "bar : 0x%x", bar);
	uint8_t cap_length = *(uint8_t*) (bar);
	ehci_op = (struct ehci_operation*) (bar + cap_length);

	stop_device();
	reset_device();
	ehci_op->frindex = 0;
	ehci_op->ctrldssegment = 0;

	ehci_op->usbcmd |= EHCI_1_MICRO_FRAME | (0b00 << 2);

	start_device();

	hcsparam = (uint32_t*) (bar + 0x4);
	hccparam = (uint32_t*) (bar + 0x8);

	log(mod, "EHCI is 64 bit : %B", *hccparam & 1);

	ehci_op->configflag = 1;

	log(mod, "EHCI setup done");

	init_periodic();

	serial2_printf("EHCI interrupt line: %d\n", device->interrupt_line);

	auto curr_vector = IOUtils::isr_get_vector(device->interrupt_line);
	serial2_printf("E1000 interrupt line: %d (%d)\n",
		       device->interrupt_line, curr_vector);

	if (device->interrupt_line) {
		auto irq = IOUtils::irq_alloc_entry();
		IOUtils::isr_map(device->interrupt_line, irq);
		IOUtils::irq_register(irq, (void*) EHCIModule::fireHandler);
	}

	ehci_op->usbsts = 0x3f;
	ehci_op->usbintr = (1 << 0) | // USBINT
			   (1 << 1) | // USBERRINT
			   (1 << 2);  // PORT CHANGE
	probe();

	log(mod, "Loaded Module");
}

extern "C" void sendAsyncCWrapper(uint32_t data_phys, size_t size) {
	instance.sendAsync(data_phys, size);
}

// uint64_t sendAsyncWithResponseCWrapper()

__attribute__((constructor)) static void ehci_constructor() {
	// UsbControllerOp op;
	// USBController usb_con;
	// // op.send = sendAsyncCWrapper;

	// usb_con.name = "EHCI";
	// usb_con.ops = &op;

	// ioforge_register_usb_controller(&usb_con);
}
