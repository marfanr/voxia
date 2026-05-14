#include "atapi/atapi.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_block.h"

IoForgeModuleConstructor(ATAPIModule);

ATAPIModule::ATAPIModule() : IOForgeBlock("ATAPI") {
}

ATAPIModule* ATAPIModule::getInstance() {
	return &instance;
}

void ATAPIModule::unload() {
}

void ATAPIModule::load() {

	log(mod, "Module Loaded");

	foreach_block_device_by_type(
		ioforge_get_block_devices_root(), IOFORGE_BLOCK_TYPE_SATAPI,
		[this](struct ioforge_block_device* dev) {
			log(mod, "found SATAPI device at %d", dev->port);
			probe(dev);
		});

	log(mod, "Device found, calling setup");
}
