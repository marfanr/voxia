#include "ioforge/ioforge_virtio.h"
#include "ioforge/ioforge_virtio.hpp"
#include "virtio-gpu/virtio-gpu.hpp"
#include "virtio/virtio.h"

IoForgeModuleConstructor(VirtioGpu);

VirtioGpu::VirtioGpu() : IoForgeVirtio("Virtio-GPU") {
}

VirtioGpu* VirtioGpu::getInstance() {
	return &instance;
}

void VirtioGpu::unload() {
}

void VirtioGpu::load() {
	log(mod, "Module Loaded");

	dev_ = find_virtio_device_by_id(0x1AF4, 0x1050);
	if (!dev_) {
		log(mod, "No Virtio-GPU device found");
		return;
	}

	log(mod, "Found Virtio-GPU device at %d:%d:%d", dev_->pci.pci_bus,
	    dev_->pci.pci_dev, dev_->pci.pci_func);
	setup();
}
