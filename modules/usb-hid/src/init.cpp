#include "ioforge/ioforge.h"
#include "ioforge/ioforge_usb.h"
#include "ioforge/ioforge_usb.hpp"
#include "usb-hid/hid.hpp"
#include <ioforge/ioforge.hpp>

IoForgeModuleConstructor(HIDModule);

HIDModule::HIDModule() : IoForgeUSB("USB-HID") {
}

HIDModule* HIDModule::getInstance() {
	return &instance;
}

void HIDModule::unload() {
}

void HIDModule::load() {

	log(mod, "HID Module Loaded");
	print_device_tree(ioforge_get_root(), 0);

	foreach_usb_device_by_devclass(ioforge_get_usb_devices_root(), 0x3,
				       [this](ioforge_usb_device* dev) {
					       log(mod, "found %s",
						   dev->base.name);
					       hid_device_setup(dev);
				       });

	// scan all usb devices
	// auto dev =
}

__attribute__((constructor)) static void hid_constructor() {
	log("HID COnstructor", " Loaded");
	// UsbControllerOp op;
	// USBController   usb_con;
	// op.send = sendAsyncCWrapper;

	// usb_con.name = "EHCI";
	// usb_con.ops  = &op;

	// ioforge_register_usb_controller(&usb_con);
}
