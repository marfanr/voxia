#include "ahci/ahci.hpp"
#include "ioforge/ioforge.h"
#include <ioforge/ioforge.hpp>

IoForgeModuleConstructor(AHCIModule);

AHCIModule::AHCIModule() : IOforgePCI("AHCI") {
}

AHCIModule* AHCIModule::getInstance() {
	return &instance;
}

void AHCIModule::unload() {
}

void AHCIModule::load() {

	log(mod, "Module Loaded");
	log(mod, "Looking for AHCI device 0x8086:0x2922");
	dev_ = findDevice(0x8086, 0x2922);
	if (!dev_) {
		log(mod, "Device not found");
		return;
	}

	log(mod, "Device found, calling setup");
	setup();
}
