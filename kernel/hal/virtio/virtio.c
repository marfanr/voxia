#include "init/init.h"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_pci.h"
#include "ioforge/ioforge_virtio.h"
#include "libk/serial.h"
#include "type.h"
#include <virtio/virtio.h>
#include <str.h>
#include <hal/pci/pci.h>

struct ioforge_device* virtio_root = 0;

static void
virtio_scan_capabilities(struct ioforge_pci_device* pci, uint16_t virtio_cap,
			 struct ioforge_virtio_device* v) {
	if (!virtio_cap) {
		LOG2_WARN("VIRTIO",
			  "device at %d:%d:%d has virtio "
			  "flag but no capability",
			  pci->pci_bus, pci->pci_dev, pci->pci_func);
		return;
	}

	uint8_t pci_bus = (uint8_t) pci->pci_bus;
	uint8_t pci_dev = (uint8_t) pci->pci_dev;
	uint8_t pci_func = (uint8_t) pci->pci_func;

	auto cap_len = pci_read8(pci_bus, pci_dev, pci_func, virtio_cap + 2);
	auto next_ptr = pci_read8(pci_bus, pci_dev, pci_func, virtio_cap + 1);
	auto cap_type = pci_read8(pci_bus, pci_dev, pci_func, virtio_cap + 3);
	auto bar = pci_read8(pci_bus, pci_dev, pci_func, virtio_cap + 4);
	auto offset = pci_read32(pci_bus, pci_dev, pci_func, virtio_cap + 8);

	LOG2_INFO("VIRTIO",
		  "  virtio cap type %d len %d bar %d "
		  "offset 0x%x",
		  cap_type, cap_len, bar, offset);

	switch (cap_type) {
	case VIRTIO_PCI_CAP_COMMON_CFG: {

		uint32_t multiplier =
			pci_read32(pci_bus, pci_dev, pci_func, virtio_cap + 12);

		auto virtio_common_cap = &v->common_cfg;
		virtio_common_cap->bar = bar;
		virtio_common_cap->offset = offset;
		virtio_common_cap->length = multiplier;
		virtio_common_cap->cfg_type = VIRTIO_PCI_CAP_COMMON_CFG;
		virtio_common_cap->cap_next = next_ptr;
		break;
	}
	case VIRTIO_PCI_CAP_NOTIFY_CFG: {
		uint32_t multiplier =
			pci_read32(pci_bus, pci_dev, pci_func, virtio_cap + 12);
		auto virtio_notify_cap = &v->notify_cfg;
		virtio_notify_cap->bar = bar;
		virtio_notify_cap->offset = offset;
		virtio_notify_cap->length = multiplier;
		virtio_notify_cap->cfg_type = VIRTIO_PCI_CAP_NOTIFY_CFG;
		virtio_notify_cap->cap_next = next_ptr;
		break;
	}
	case VIRTIO_PCI_CAP_ISR_CFG: {
		uint32_t multiplier =
			pci_read32(pci_bus, pci_dev, pci_func, virtio_cap + 12);
		auto virtio_isr_cap = &v->isr_cfg;
		virtio_isr_cap->bar = bar;
		virtio_isr_cap->offset = offset;
		virtio_isr_cap->length = multiplier;
		virtio_isr_cap->cfg_type = VIRTIO_PCI_CAP_ISR_CFG;
		virtio_isr_cap->cap_next = next_ptr;
		break;
	}
	}
}

static void for_each_virtio_device(struct ioforge_device* node) {
	if (!node)
		return;

	if (node->type == IOFORGE_PCI) {
		struct ioforge_pci_device* pci =
			(struct ioforge_pci_device*) node;

		if (pci->base.flags & (uint32_t)IOFORGE_F_VIRTIO) {

			LOG2_INFO("VIRTIO", "found virtio device at %d:%d:%d",
				  pci->pci_bus, pci->pci_dev, pci->pci_func);

			struct ioforge_virtio_device* virtio_device =
				(struct ioforge_virtio_device*) kalloc(
					sizeof(*virtio_device));
			memset(virtio_device, 0, sizeof(*virtio_device));
			memcopy(&virtio_device->pci, pci, sizeof(*pci));
			virtio_device->pci.base.type = IOFORGE_VIRTIO;

			{
				auto cap_ptr = pci->capability_ptr;
				uint8_t bus = (uint8_t) pci->pci_bus;
				uint8_t device = (uint8_t) pci->pci_dev;
				uint8_t func = (uint8_t) pci->pci_func;

				while (cap_ptr != 0 && cap_ptr >= 0x40
				       && cap_ptr <= 0xFF) {
					uint8_t cap_id = pci_read8(
						bus, device, func, cap_ptr);
					uint8_t next_ptr = pci_read8(
						bus, device, func, cap_ptr + 1);

					if (cap_id == VIRTIO_PCI_CAP) {
						virtio_scan_capabilities(
							pci, cap_ptr,
							virtio_device);
					}

					cap_ptr = next_ptr;
				}
			}

			if (!virtio_device->common_cfg.bar
			    || !virtio_device->notify_cfg.bar
			    || !virtio_device->isr_cfg.bar) {
				LOG2_ERROR(
					"VIRTIO",
					"Missing required VirtIO capabilities");
				kfree(virtio_device, sizeof(*virtio_device));
				goto for_each_virtio_next;
			}

			// check
			{
				uint8_t common_bar =
					virtio_device->common_cfg.bar & 0x7;
				if (common_bar >= 6
				    || virtio_device->pci.bar[common_bar]
					       .iospace) {
					LOG2_ERROR(
						"VIRTIO",
						"Invalid common config BAR: %d",
						common_bar);
					return;
				}
			}
			{
				uint8_t notify_bar =
					virtio_device->notify_cfg.bar & 0x7;
				if (notify_bar >= 6
				    || virtio_device->pci.bar[notify_bar]
					       .iospace) {
					LOG2_ERROR("VIRTIO",
						   "Invalid notify BAR: %d",
						   notify_bar);
					return;
				}
			}
			{
				uint8_t isr_bar =
					virtio_device->isr_cfg.bar & 0x7;
				if (isr_bar >= 6
				    || virtio_device->pci.bar[isr_bar]
					       .iospace) {
					LOG2_ERROR("VIRTIO",
						   "Invalid ISR BAR: %d",
						   isr_bar);
					return;
				}
			}

			ioforge_attach(virtio_root, &virtio_device->pci.base);
		}
	}

for_each_virtio_next:
	for_each_virtio_device(node->first_child);
	for_each_virtio_device(node->next_sibling);
}

INIT(Virtio) {
	LOG2_INFO("VIRTIO", "virtio init");

	virtio_root = kalloc(sizeof(struct ioforge_device));
	memset(virtio_root, 0, sizeof(*virtio_root));
	strcpy((char*) virtio_root->name, "VIRTIO");
	virtio_root->type = IOFORGE_ROOT;
	ioforge_attach(ioforge_get_root(), virtio_root);

	auto pci_root = ioforge_get_pci_root();
	LOG2_INFO("VIRTIO",
		  "scanning for virtio devices under PCI root at 0x%x",
		  (uintptr_t) pci_root);
	for_each_virtio_device(pci_root);
}

KERNEL_API struct ioforge_virtio_device*
find_virtio_device_by_id(uint16_t vendor_id, uint16_t device_id) {
	struct ioforge_device* node = virtio_root->first_child;
	while (node) {
		if (node->type == IOFORGE_VIRTIO) {
			struct ioforge_virtio_device* v =
				(struct ioforge_virtio_device*) (void*) node;
			if (v->pci.vendor_id == vendor_id
			    && v->pci.device_id == device_id) {
				return v;
			}
		}
		node = node->next_sibling;
	}
	return NULL;
}