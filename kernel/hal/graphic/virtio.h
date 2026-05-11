#ifndef __HAL_GRAPHIC_VIRTIO_H__
#define __HAL_GRAPHIC_VIRTIO_H__

#include <ioforge/ioforge_pci.h>
#include <type.h>

void virtio_gpu_init(struct ioforge_pci_device* pci_device);

#endif // __HAL_GRAPHIC_VIRTIO_H__
