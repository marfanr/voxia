// #include "hal/virtio/virtio.h"
// #include "hal/pci/pci.h"
// #include "ioforge/ioforge_pci.h"
// #include <str.h>

// virtio_device_t *
// VxVirtioInit(struct ioforge_pci_service *pci_device)
// {
//     auto virtio_dev = (virtio_device_t *)kalloc(sizeof(virtio_device_t));
//     memset(virtio_dev, 0, sizeof(virtio_device_t));
//     return virtio_dev;
// }

// static struct virtio_pci_cap *
// find_virtio_capability(struct ioforge_pci_service *pci_device, uint8_t cfg_type)
// {
//     // Start from capability pointer
//     uint8_t cap_ptr = pci_device->capability_ptr;

//     while (cap_ptr != 0 && cap_ptr >= 0x40) // Capabilities start at 0x40
//     {
//         uint8_t cap_id   = pci_readb(pci_device->bus, pci_device->device, pci_device->function,
//                                      cap_ptr); // ID capability
//         uint8_t next_ptr = pci_readb(pci_device->bus, pci_device->device, pci_device->function,
//                                      cap_ptr + 1); // pointer ke capability selanjutnya

//         // LOG_INFO("PCI", "   Capability ID: 0x%x at offset 0x%x", cap_id, cap_ptr);

//         // Hanya baca VirtIO PCI (cap_id = 0x09)
//         if (cap_id == 0x09)
//         {
//             uint8_t len = pci_readb(pci_device->bus, pci_device->device, pci_device->function,
//                                     cap_ptr + 2); // panjang capability
//             // LOG_INFO("PCI", "cap length %d", len);
//             uint8_t type = pci_readb(pci_device->bus, pci_device->device, pci_device->function,
//                                      cap_ptr + 3); // type VirtIO
//             uint8_t bar  = pci_readb(pci_device->bus, pci_device->device, pci_device->function,
//                                      cap_ptr + 4); // BAR yang dipakai

//             uint32_t offset = pci_readl(pci_device->bus, pci_device->device, pci_device->function,
//                                         cap_ptr + 8); // offset register
//             // LOG_INFO("PCI", "   VirtIO type %d BAR: %d, Offset: 0x%x", type, bar, offset);
//             if (type == cfg_type)
//             {
//                 uint32_t multiplier = pci_readl(pci_device->bus, pci_device->device,
//                                                 pci_device->function, cap_ptr + 12);
//                 // LOG_INFO("PCI", "multiplier %d", multiplier);
//                 struct virtio_pci_cap *cap =
//                     (struct virtio_pci_cap *)kalloc(sizeof(struct virtio_pci_cap));
//                 cap->bar      = bar;
//                 cap->offset   = offset;
//                 cap->length   = multiplier;
//                 cap->cfg_type = cfg_type;
//                 cap->cap_next = next_ptr;
//                 cap->offset   = offset;

//                 return cap;
//             }
//         }

//         cap_ptr = next_ptr;
//     }

//     return NULL;
// }
