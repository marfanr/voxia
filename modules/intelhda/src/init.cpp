#include "ioforge/ioforge.h"
#include "ioforge/ioforge_usb.h"
#include "ioforge/ioforge_usb.hpp"
#include "usb-hid/hid.hpp"
#include "usb-hid/keyboard.hpp"
#include <ioforge/ioforge.hpp>

IoForgeModuleConstructor(UsbHid);

UsbHid::UsbHid() : IoForgeUSB("USB-HID") {
}

UsbHid* UsbHid::getInstance() {
	return &instance;
}

void UsbHid::unload() {
}

void UsbHid::load() {

	log(mod, "HID Module Loaded");
	print_device_tree(ioforge_get_root(), 2);

	foreach_usb_device_by_devclass(ioforge_get_usb_devices_root(), 0x3,
				       [this](ioforge_usb_device* dev) {
					       log(mod, "found %s",
						   dev->base.name);
					       hid_device_setup(dev);
				       });

	// scan all usb devices
	// auto dev =
}
