#include "sata/sata.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_block.h"

IoForgeModuleConstructor(SATAModule);

SATAModule::SATAModule() : IOForgeBlock("SATA") {
}

SATAModule* SATAModule::getInstance() {
	return &instance;
}

void SATAModule::unload() {
}

void SATAModule::load() {

	log(mod, "Module Loaded");

	foreach_by_type(
		ioforge_get_block_devices_root(), IOFORGE_BLOCK_TYPE_SATA,
		[this](struct ioforge_block_device* dev) {
			log(mod, "found SATA device at %d", dev->port);
			probe(dev);
		});
}
