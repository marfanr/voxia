#ifndef __HAL_GRAPHIC_VIRTIO_H__
#define __HAL_GRAPHIC_VIRTIO_H__

#include <libk/type.h>
#include <sys/ioforge/ioforge_pci.h>

void virtio_gpu_init(struct IoForgePCI *pci_device);

#endif // __HAL_GRAPHIC_VIRTIO_H__
