#ifndef __IOFORGE__IOFORGE_VIRTIO_H__
#define __IOFORGE__IOFORGE_VIRTIO_H__

#include "ioforge/ioforge.h"
#include "ioforge/ioforge_pci.h"
#include "virtio/virtio.h"
#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ioforge_virtio_device {
	struct ioforge_pci_device pci;
	struct virtio_pci_cap common_cfg;
	struct virtio_pci_cap notify_cfg;
	struct virtio_pci_cap isr_cfg;
	struct virtio_pci_cap device_cfg;
	struct virtio_pci_cap pci_cfg;
} __attribute__((aligned(64)));

#ifdef __cplusplus
}
#endif

#endif // __IOFORGE__IOFORGE_VIRTIO_H__