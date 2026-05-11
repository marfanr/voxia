#ifndef __HAL__VIRTIO__VIRTIO_H__
#define __HAL__VIRTIO__VIRTIO_H__

#include "ioforge/ioforge_pci.h"
#include "libk/type.h"

typedef struct
{
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;

    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} __attribute__((packed)) virtio_pci_common_cfg_t;

typedef struct
{
    struct virtq_desc  *desc;
    struct virtq_avail *avail;
    struct virtq_used  *used;
    uint16_t            queue_size;
    uint16_t            free_head;
    uint16_t            num_free;
    uint16_t            last_used_idx;
    uintptr_t           phys_addr;
} virtio_queue_t;

typedef struct
{
    virtio_pci_common_cfg_t *common_cfg;
    volatile uint32_t       *notify_base;
    volatile uint8_t        *isr;
    uintptr_t                notify_offset;
    uintptr_t                notify_multiplier;
    virtio_queue_t           controlq;
    uint32_t                 features;
    bool                     initialized;
} virtio_device_t;

struct virtio_pci_cap
{
    uint8_t  cap_vndr;
    uint8_t  cap_next;
    uint8_t  cap_len;
    uint8_t  cfg_type;
    uint8_t  bar;
    uint8_t  padding[3];
    uint32_t offset;
    uint32_t length;
} __attribute__((packed));

virtio_device_t *VxVirtioInit(struct ioforge_pci_service *pci_device);

#endif // __HAL__VIRTIO__VIRTIO_H__
