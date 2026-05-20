// ini works, tapi perlu penyesuaian PCI API baru

// #include "hal/cpu/paging.h"
// #include "ioforge/ioforge_pci.h"
// #include "libk/io.h"
// #include <type.h>
// #include "memory/kalloc.h"
// #include "memory/memory_utils.h"
// #include "memory/phys_base_allocator.h"
// #include "memory/vm_manager.h"
// #include <hal/graphic/virtio.h>
// #include <hal/pci/pci.h>
// #include <libk/serial.h>
// #include <str.h>

// boolean_t irq_trigered = false;

// // VirtIO GPU Command Types
// #define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
// #define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
// #define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102
// #define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
// #define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
// #define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
// #define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
// #define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107

// // Response Types
// #define VIRTIO_GPU_RESP_OK_NODATA 0x1100
// #define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101

// // Formats
// #define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1
// #define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2
// #define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM 3

// // VirtIO Device Status bits
// #define VIRTIO_STATUS_ACKNOWLEDGE (1 << 0)
// #define VIRTIO_STATUS_DRIVER (1 << 1)
// #define VIRTIO_STATUS_DRIVER_OK (1 << 2)
// #define VIRTIO_STATUS_FEATURES_OK (1 << 3)
// #define VIRTIO_STATUS_DEVICE_NEEDS_RESET (1 << 6)
// #define VIRTIO_STATUS_FAILED (1 << 7)

// // Descriptor Flags
// #define VIRTQ_DESC_F_NEXT (1 << 0)
// #define VIRTQ_DESC_F_WRITE (1 << 1)
// #define VIRTQ_DESC_F_INDIRECT (1 << 2)

// // ISR Status bits
// #define VIRTIO_ISR_QUEUE_INT (1 << 0)
// #define VIRTIO_ISR_DEV_CFG_INT (1 << 1)

// // ---------------- VirtIO PCI Common Config (BAR0) ----------------
// struct virtio_pci_common_cfg
// {
//     uint32_t device_feature_select;
//     uint32_t device_feature;
//     uint32_t driver_feature_select;
//     uint32_t driver_feature;
//     uint16_t msix_config;
//     uint16_t num_queues;
//     uint8_t  device_status;
//     uint8_t  config_generation;

//     uint16_t queue_select;
//     uint16_t queue_size;
//     uint16_t queue_msix_vector;
//     uint16_t queue_enable;
//     uint16_t queue_notify_off;
//     uint64_t queue_desc;
//     uint64_t queue_avail;
//     uint64_t queue_used;
// } __attribute__((packed));

// // ---------------- Virtqueue Structures ----------------
// #define VIRTIO_GPU_QUEUE_SIZE 64
// #define VIRTIO_GPU_QUEUE_ALIGN 4096

// struct virtq_desc
// {
//     uint64_t addr;
//     uint32_t len;
//     uint16_t flags;
//     uint16_t next;
// } __attribute__((packed));

// struct virtq_avail
// {
//     uint16_t flags;
//     uint16_t idx;
//     uint16_t ring[VIRTIO_GPU_QUEUE_SIZE];
//     uint16_t used_event;
// } __attribute__((packed));

// struct virtq_used_elem
// {
//     uint32_t id;
//     uint32_t len;
// } __attribute__((packed));

// struct virtq_used
// {
//     uint16_t               flags;
//     uint16_t               idx;
//     struct virtq_used_elem ring[VIRTIO_GPU_QUEUE_SIZE];
//     uint16_t               avail_event;
// } __attribute__((packed));

// struct virtio_gpu_queue
// {
//     struct virtq_desc  *desc;
//     struct virtq_avail *avail;
//     struct virtq_used  *used;
//     uint16_t            queue_size;
//     uint16_t            free_head;
//     uint16_t            num_free;
//     uint16_t            last_used_idx;
//     uintptr_t           phys_addr;
// };

// // ---------------- VirtIO GPU Structures ----------------
// struct virtio_gpu_ctrl_hdr
// {
//     uint32_t type;
//     uint32_t flags;
//     uint64_t fence_id;
//     uint32_t ctx_id;
//     uint32_t padding;
// } __attribute__((packed));

// struct virtio_gpu_rect
// {
//     uint32_t x;
//     uint32_t y;
//     uint32_t width;
//     uint32_t height;
// } __attribute__((packed));

// struct virtio_gpu_resp_display_info
// {
//     struct virtio_gpu_ctrl_hdr hdr;
//     struct virtio_gpu_display_one
//     {
//         struct virtio_gpu_rect rect;
//         uint32_t               enabled;
//         uint32_t               flags;
//     } pmodes[16];
// } __attribute__((packed));

// struct virtio_gpu_resource_create_2d
// {
//     struct virtio_gpu_ctrl_hdr hdr;
//     uint32_t                   resource_id;
//     uint32_t                   format;
//     uint32_t                   width;
//     uint32_t                   height;
// } __attribute__((packed));

// struct virtio_gpu_resource_attach_backing
// {
//     struct virtio_gpu_ctrl_hdr hdr;
//     uint32_t                   resource_id;
//     uint32_t                   nr_entries;
// } __attribute__((packed));

// struct virtio_gpu_mem_entry
// {
//     uint64_t addr;
//     uint32_t length;
//     uint32_t padding;
// } __attribute__((packed));

// struct virtio_gpu_set_scanout
// {
//     struct virtio_gpu_ctrl_hdr hdr;
//     struct virtio_gpu_rect     rect;
//     uint32_t                   scanout_id;
//     uint32_t                   resource_id;
// } __attribute__((packed));

// struct virtio_gpu_transfer_to_host_2d
// {
//     struct virtio_gpu_ctrl_hdr hdr;
//     struct virtio_gpu_rect     rect;
//     uint64_t                   offset;
//     uint32_t                   resource_id;
//     uint32_t                   padding;
// } __attribute__((packed));

// struct virtio_gpu_resource_flush
// {
//     struct virtio_gpu_ctrl_hdr hdr;
//     struct virtio_gpu_rect     rect;
//     uint32_t                   resource_id;
//     uint32_t                   padding;
// } __attribute__((packed));

// // VirtIO PCI Capability Structure
// struct virtio_pci_cap
// {
//     uint8_t  cap_vndr;
//     uint8_t  cap_next;
//     uint8_t  cap_len;
//     uint8_t  cfg_type;
//     uint8_t  bar;
//     uint8_t  padding[3];
//     uint32_t offset;
//     uint32_t length;
// } __attribute__((packed));

// // Driver state
// struct virtio_gpu_device
// {
//     struct virtio_pci_common_cfg *common_cfg;
//     volatile uint32_t            *notify_base;
//     volatile uint8_t             *isr;
//     uintptr_t                     notify_offset;
//     uintptr_t                     notify_multiplier;
//     struct virtio_gpu_queue       controlq;
//     uint32_t                      features;
//     bool                          initialized;
// };

// static struct virtio_gpu_device gpu_dev = {0};

// // Helper function to get physical address from virtual
// static uintptr_t
// virt_to_phys(void *virt_addr)
// {
//     // This needs to be implemented based on your memory management
//     // For now, assuming identity mapping for DMA allocations
//     LOG_DEBUG("V2P", "virt addr 0x%x", (uintptr_t)virt_addr);
//     return (uintptr_t)virt_addr;
// }

// // ---------------- PCI Capability Discovery ----------------
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

// // ---------------- VirtQueue Management ----------------
// static uint16_t
// virtq_alloc_desc(struct virtio_gpu_queue *vq)
// {
//     if (vq->num_free == 0)
//     {
//         LOG_ERROR("VIRTIO", "No free descriptors");
//         return vq->queue_size;
//     }

//     uint16_t desc_idx = vq->free_head;
//     vq->free_head     = vq->desc[desc_idx].next;
//     vq->num_free--;

//     // Clear the descriptor
//     vq->desc[desc_idx].addr  = 0;
//     vq->desc[desc_idx].len   = 0;
//     vq->desc[desc_idx].flags = 0;
//     vq->desc[desc_idx].next  = 0;

//     return desc_idx;
// }

// static void
// virtq_free_desc(struct virtio_gpu_queue *vq, uint16_t desc_idx)
// {
//     vq->desc[desc_idx].next = vq->free_head;
//     vq->free_head           = desc_idx;
//     vq->num_free++;
// }
// static void
// virtq_init(struct virtio_gpu_queue *vq, void *vq_mem, uint16_t queue_size, uintptr_t phys_addr)
// {
//     vq->desc  = (struct virtq_desc *)vq_mem;
//     vq->avail = (struct virtq_avail *)((uintptr_t)vq_mem + queue_size * sizeof(struct virtq_desc));

//     // Calculate used ring with proper alignment
//     uintptr_t used_offset = (uintptr_t)vq->avail + sizeof(uint16_t) * (3 + queue_size);
//     used_offset           = ALIGN_UP(used_offset, VIRTIO_GPU_QUEUE_ALIGN);
//     vq->used              = (struct virtq_used *)used_offset;

//     vq->queue_size    = queue_size;
//     vq->free_head     = 0;
//     vq->num_free      = queue_size;
//     vq->last_used_idx = 0;
//     vq->phys_addr     = phys_addr;

//     // Initialize descriptor chain
//     for (uint16_t i = 0; i < queue_size - 1; i++)
//     {
//         vq->desc[i].next = i + 1;
//     }
//     vq->desc[queue_size - 1].next = 0;

//     // Initialize rings
//     memset(vq->avail, 0, sizeof(struct virtq_avail) + sizeof(uint16_t) * queue_size);
//     memset(vq->used, 0, sizeof(struct virtq_used) + sizeof(struct virtq_used_elem) * queue_size);

//     vq->avail->idx = 0;
//     vq->used->idx  = 0;

//     LOG_DEBUG("VIRTIO", "Virtqueue initialized: desc=%x, avail=%x, used=%x", vq->desc, vq->avail,
//               vq->used);
// }
// static int
// virtq_add_buf(struct virtio_gpu_queue *vq, void **buffers, uint32_t *lengths, uint16_t num_out,
//               uint16_t num_in, uint16_t *head_out)
// {
//     if (num_out + num_in == 0 || num_out + num_in > vq->num_free)
//     {
//         LOG_ERROR("VIRTIO", "Not enough descriptors: free=%d, needed=%d", vq->num_free,
//                   num_out + num_in);
//         return -1;
//     }

//     uint16_t head = virtq_alloc_desc(vq);
//     uint16_t prev = head;
//     *head_out     = head;

//     LOG_DEBUG("VIRTIO", "Building descriptor chain, head=%d, num_out=%d, num_in=%d", head, num_out,
//               num_in);

//     // Add output buffers (device reads)
//     for (uint16_t i = 0; i < num_out; i++)
//     {
//         uint64_t phys_addr   = virt_to_phys(buffers[i]);
//         vq->desc[prev].addr  = phys_addr;
//         vq->desc[prev].len   = lengths[i];
//         vq->desc[prev].flags = VIRTQ_DESC_F_NEXT;

//         LOG_DEBUG("VIRTIO", "OUT desc[%d]: phys=0x%lx, len=%d, flags=0x%x, next=%d", prev,
//                   phys_addr, lengths[i], vq->desc[prev].flags, vq->desc[prev].next);

//         if (i < num_out - 1)
//         {
//             prev                    = virtq_alloc_desc(vq);
//             vq->desc[prev - 1].next = prev;
//         }
//     }

//     // Add input buffers (device writes)
//     for (uint16_t i = 0; i < num_in; i++)
//     {
//         uint16_t idx;
//         if (i == 0 && num_out == 0)
//         {
//             idx = head;
//         }
//         else
//         {
//             idx                 = virtq_alloc_desc(vq);
//             vq->desc[prev].next = idx;
//             prev                = idx;
//         }

//         uint64_t phys_addr  = virt_to_phys(buffers[num_out + i]);
//         vq->desc[idx].addr  = phys_addr;
//         vq->desc[idx].len   = lengths[num_out + i];
//         vq->desc[idx].flags = VIRTQ_DESC_F_WRITE;

//         LOG_DEBUG("VIRTIO", "IN desc[%d]: phys=0x%lx, len=%d, flags=0x%x", idx, phys_addr,
//                   lengths[num_out + i], vq->desc[idx].flags);

//         if (i < num_in - 1)
//         {
//             vq->desc[idx].flags |= VIRTQ_DESC_F_NEXT;
//             prev = idx;
//         }
//     }

//     // Remove NEXT flag from last descriptor
//     if (num_in > 0)
//     {
//         vq->desc[prev].flags &= ~VIRTQ_DESC_F_NEXT;
//         LOG_DEBUG("VIRTIO", "Last IN desc[%d]: flags=0x%x (NEXT removed)", prev,
//                   vq->desc[prev].flags);
//     }
//     else if (num_out > 0)
//     {
//         vq->desc[prev].flags &= ~VIRTQ_DESC_F_NEXT;
//         LOG_DEBUG("VIRTIO", "Last OUT desc[%d]: flags=0x%x (NEXT removed)", prev,
//                   vq->desc[prev].flags);
//     }

//     return 0;
// }

// static void
// virtq_kick(struct virtio_gpu_device *dev, uint16_t queue_index)
// {
//     // Strong memory barrier before notifying device
//     asm volatile("mfence" ::: "memory");

//     uint32_t           notify_addr = dev->notify_offset + queue_index * dev->notify_multiplier;
//     volatile uint32_t *notify_reg =
//         (volatile uint32_t *)((uintptr_t)dev->notify_base + notify_addr);

//     LOG_DEBUG("VIRTIO",
//               "Notifying queue %d at address 0x%x (notify_base=%p, offset=0x%lx, multiplier=0x%lx)",
//               queue_index, notify_addr, dev->notify_base, dev->notify_offset,
//               dev->notify_multiplier);

//     // Write the queue index to notify the device
//     *notify_reg = queue_index;

//     // Strong memory barrier after notification
//     asm volatile("mfence" ::: "memory");
// }

// static int
// virtq_get_used_elem(struct virtio_gpu_queue *vq, uint16_t *id, uint32_t *len)
// {
//     // Memory barrier to ensure we see updated used ring
//     asm volatile("" ::: "memory");

//     if (vq->last_used_idx == vq->used->idx)
//     {
//         return -1;
//     }

//     uint16_t                used_idx = vq->last_used_idx % vq->queue_size;
//     struct virtq_used_elem *elem     = &vq->used->ring[used_idx];

//     *id  = elem->id;
//     *len = elem->len;
//     vq->last_used_idx++;

//     return 0;
// }

// // ---------------- VirtIO GPU Commands ----------------
// static int
// virtio_gpu_send_command(struct virtio_gpu_device *dev, void *cmd, uint32_t cmd_size, void *resp,
//                         uint32_t resp_size)
// {
//     if (!dev->initialized)
//     {
//         LOG_ERROR("VIRTIO", "Device not initialized");
//         return -1;
//     }

//     // Use local arrays instead of dynamic allocation
//     void    *buffers[2];
//     uint32_t lengths[2];
//     uint16_t num_out = 0, num_in = 0;

//     // Setup command buffer (output - device reads)
//     if (cmd && cmd_size > 0)
//     {
//         buffers[0] = cmd;
//         lengths[0] = cmd_size;
//         num_out    = 1;
//         LOG_DEBUG("VIRTIO", "Command buffer: virt=%x, size=%d", cmd, cmd_size);
//     }

//     // Setup response buffer (input - device writes)
//     if (resp && resp_size > 0)
//     {
//         buffers[num_out] = resp;
//         lengths[num_out] = resp_size;
//         num_in           = 1;
//         LOG_DEBUG("VIRTIO", "Response buffer: virt=%x, size=%d", resp, resp_size);
//     }

//     uint16_t head;
//     if (virtq_add_buf(&dev->controlq, buffers, lengths, num_out, num_in, &head) < 0)
//     {
//         LOG_ERROR("VIRTIO", "Failed to add buffer to virtqueue");
//         return -1;
//     }

//     // Add to available ring
//     uint16_t avail_idx                   = dev->controlq.avail->idx % dev->controlq.queue_size;
//     dev->controlq.avail->ring[avail_idx] = head;

//     // Memory barrier before updating avail->idx
//     asm volatile("mfence" ::: "memory");

//     dev->controlq.avail->idx++;

//     LOG_DEBUG("VIRTIO", "Added buffer head=%d to avail ring[%d], new avail->idx=%d", head,
//               avail_idx, dev->controlq.avail->idx);

//     // Notify device
//     virtq_kick(dev, 0);

//     // Wait for response - improved polling
//     uint16_t used_id;
//     uint32_t used_len;
//     int      timeout = 5000000; // Increase timeout

//     for (int i = 0; i < timeout; i++)
//     {
//         // Check ISR status more frequently
//         if (dev->isr)
//         {
//             // uint8_t isr_status = *dev->isr;
//             // if (isr_status & VIRTIO_ISR_QUEUE_INT)
//             if (irq_trigered)
//             {
//                 LOG_DEBUG("VIRTIO", "ISR triggered");
//                 // *dev->isr = isr_status; // Acknowledge by writing back
//                 // break;

//                 irq_trigered = false;
//             }
//         }

//         // Check used ring with memory barrier
//         asm volatile("" ::: "memory");
//         if (dev->controlq.last_used_idx != dev->controlq.used->idx)
//         {
//             LOG_DEBUG("VIRTIO", "Used ring updated: last_used_idx=%d, used->idx=%d",
//                       dev->controlq.last_used_idx, dev->controlq.used->idx);
//         }

//         if (virtq_get_used_elem(&dev->controlq, &used_id, &used_len) == 0)
//         {
//             LOG_DEBUG("VIRTIO", "Got used element: id=%d, len=%d (expected head=%d)", used_id,
//                       used_len, head);

//             if (used_id == head)
//             {
//                 // Free descriptors
//                 uint16_t desc_idx = used_id;
//                 while (desc_idx < dev->controlq.queue_size &&
//                        (dev->controlq.desc[desc_idx].flags & VIRTQ_DESC_F_NEXT))
//                 {
//                     uint16_t next = dev->controlq.desc[desc_idx].next;
//                     virtq_free_desc(&dev->controlq, desc_idx);
//                     desc_idx = next;
//                 }
//                 if (desc_idx < dev->controlq.queue_size)
//                 {
//                     virtq_free_desc(&dev->controlq, desc_idx);
//                 }
//                 LOG_INFO("VIRTIO", "Command completed successfully");
//                 return 0;
//             }
//             else
//             {
//                 LOG_WARN("VIRTIO", "Unexpected used id: %d (expected %d)", used_id, head);
//                 // Continue waiting for our descriptor
//             }
//         }

//         // Shorter delay for more responsive polling
//         for (volatile int j = 0; j < 100; j++)
//             ;

//         if (i % 100000 == 0)
//         {
//             LOG_DEBUG("VIRTIO", "Still waiting for response... (timeout=%d)", i);
//         }
//     }

//     LOG_ERROR("VIRTIO", "Timeout waiting for command response (head=%d)", head);

//     // Debug: dump used ring state
//     LOG_DEBUG("VIRTIO", "Used ring state: last_used_idx=%d, used->idx=%d",
//               dev->controlq.last_used_idx, dev->controlq.used->idx);

//     // Debug: dump descriptor chain
//     LOG_DEBUG("VIRTIO", "Descriptor chain for head=%d:", head);
//     uint16_t desc_idx  = head;
//     int      chain_len = 0;
//     while (desc_idx < dev->controlq.queue_size && chain_len < 10)
//     {
//         LOG_DEBUG("VIRTIO", "  desc[%d]: addr=0x%lx, len=%d, flags=0x%x, next=%d", desc_idx,
//                   dev->controlq.desc[desc_idx].addr, dev->controlq.desc[desc_idx].len,
//                   dev->controlq.desc[desc_idx].flags, dev->controlq.desc[desc_idx].next);

//         if (!(dev->controlq.desc[desc_idx].flags & VIRTQ_DESC_F_NEXT))
//             break;

//         desc_idx = dev->controlq.desc[desc_idx].next;
//         chain_len++;
//     }

//     return -1;
// }

// int
// virtio_gpu_get_display_info(struct virtio_gpu_device *dev)
// {
//     struct virtio_gpu_ctrl_hdr *cmd = (struct virtio_gpu_ctrl_hdr *)pDMAalloc(1);
//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)cmd, (uintptr_t)cmd, 1,
//                    PAGE_PRESENT | PAGE_WRITABLE);

//     cmd->ctx_id   = 0;
//     cmd->type     = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
//     cmd->flags    = 0;
//     cmd->fence_id = 0;
//     cmd->padding  = 0;

//     struct virtio_gpu_resp_display_info *resp = (struct virtio_gpu_resp_display_info *)pDMAalloc(1);
//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)resp, (uintptr_t)resp, 1,
//                    PAGE_PRESENT | PAGE_WRITABLE);

//     memset(resp, 0, sizeof(*resp));

//     LOG_DEBUG("VIRTIO", "Sending GET_DISPLAY_INFO command");
//     if (virtio_gpu_send_command(dev, cmd, sizeof(*cmd), resp, sizeof(*resp)) < 0)
//     {
//         LOG_ERROR("VIRTIO", "Failed to send GET_DISPLAY_INFO command");
//         return -1;
//     }

//     if (resp->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
//     {
//         LOG_ERROR("VIRTIO", "Bad response type: 0x%x (expected 0x%x)", resp->hdr.type,
//                   VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
//         return -1;
//     }

//     LOG_INFO("VIRTIO", "Display info received:");
//     int enabled_count = 0;
//     for (int i = 0; i < 16; i++)
//     {
//         if (resp->pmodes[i].enabled)
//         {
//             LOG_INFO("VIRTIO", "  Scanout %d: %dx%d at (%d,%d)", i, resp->pmodes[i].rect.width,
//                      resp->pmodes[i].rect.height, resp->pmodes[i].rect.x, resp->pmodes[i].rect.y);
//             enabled_count++;
//         }
//     }

//     if (enabled_count == 0)
//     {
//         LOG_WARN("VIRTIO", "No enabled display modes found");
//     }

//     return 0;
// }

// int
// virtio_gpu_create_resource(struct virtio_gpu_device *dev, uint32_t resource_id, uint32_t width,
//                            uint32_t height)
// {
//     struct virtio_gpu_resource_create_2d *cmd =
//         (struct virtio_gpu_resource_create_2d *)pDMAalloc(1);
//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)cmd, (uintptr_t)cmd, 1,
//                    PAGE_PRESENT | PAGE_WRITABLE);
//     cmd->hdr.type     = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
//     cmd->hdr.flags    = 0;
//     cmd->hdr.fence_id = 0;
//     cmd->hdr.ctx_id   = 0;
//     cmd->hdr.padding  = 0;
//     cmd->resource_id  = resource_id;
//     cmd->format       = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
//     cmd->width        = width;
//     cmd->height       = height;

//     struct virtio_gpu_ctrl_hdr *resp = (struct virtio_gpu_ctrl_hdr *)pDMAalloc(1);
//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)resp, (uintptr_t)resp, 1,
//                    PAGE_PRESENT | PAGE_WRITABLE);

//     memset(resp, 0, sizeof(*resp));

//     LOG_DEBUG("VIRTIO", "Creating resource %d: %dx%d", resource_id, width, height);
//     if (virtio_gpu_send_command(dev, cmd, sizeof(*cmd), resp, sizeof(*resp)) < 0)
//     {
//         LOG_ERROR("VIRTIO", "Failed to create resource");
//         return -1;
//     }

//     if (resp->type != VIRTIO_GPU_RESP_OK_NODATA)
//     {
//         LOG_ERROR("VIRTIO", "Resource create failed: 0x%x", resp->type);
//         return -1;
//     }

//     LOG_INFO("VIRTIO", "Resource %d created: %dx%d", resource_id, width, height);
//     return 0;
// }

// // ---------------- Main Initialization ----------------
// void
// virtio_gpu_init(struct ioforge_pci_service *pci_device)
// {
//     memset(&gpu_dev, 0, sizeof(gpu_dev));

//     LOG_INFO("VIRTIO", "Initializing VirtIO GPU device (vendor: 0x%04x, device: 0x%04x)",
//              pci_device->vendor_id, pci_device->device_id);

//     // Find VirtIO capabilities
//     struct virtio_pci_cap *common_cfg = find_virtio_capability(pci_device, 1); // Common config
//     LOG_DEBUG("VIRTIO", "Found common config capability: %x", common_cfg);

//     struct virtio_pci_cap *notify_cfg = find_virtio_capability(pci_device, 2); // Notify
//     struct virtio_pci_cap *isr_cfg    = find_virtio_capability(pci_device, 3); // ISR

//     if (!common_cfg || !notify_cfg || !isr_cfg)
//     {
//         LOG_ERROR("VIRTIO", "Missing required VirtIO capabilities");
//         return;
//     }

//     LOG_DEBUG("VIRTIO", "Found VirtIO capabilities: common=%x, notify=%x, isr=%x", common_cfg,
//               notify_cfg, isr_cfg);

//     // Map common configuration
//     uint8_t common_bar = common_cfg->bar & 0x7;
//     if (common_bar >= 6 || pci_device->bar[common_bar].iospace)
//     {
//         LOG_ERROR("VIRTIO", "Invalid common config BAR: %d", common_bar);
//         return;
//     }

//     uintptr_t common_phys =
//         (pci_device->bar[3].address << 32 | (pci_device->bar[2].address & ~0xF)) +
//         common_cfg->offset;
//     gpu_dev.common_cfg = (struct virtio_pci_common_cfg *)pDMAalloc(1);
//     if (!gpu_dev.common_cfg)
//     {
//         LOG_ERROR("VIRTIO", "Failed to allocate memory for common config");
//         return;
//     }
//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)gpu_dev.common_cfg, common_phys,
//                    BLOCK_SIZE, PAGE_PRESENT | PAGE_WRITABLE);

//     // Map notify region
//     uint8_t notify_bar = notify_cfg->bar & 0x7;
//     if (notify_bar >= 6 || pci_device->bar[notify_bar].iospace)
//     {
//         LOG_ERROR("VIRTIO", "Invalid notify BAR: %d", notify_bar);
//         return;
//     }

//     uintptr_t notify_phys =
//         (pci_device->bar[3].address << 32 | (pci_device->bar[2].address & ~0xF)) +
//         notify_cfg->offset;
//     // uintptr_t notify_phys = pci_device->bar[notify_bar].address + notify_cfg->offset;
//     gpu_dev.notify_base = (volatile uint32_t *)pDMAalloc(3);
//     if (!gpu_dev.notify_base)
//     {
//         LOG_ERROR("VIRTIO", "Failed to allocate memory for notify region");
//         return;
//     }
//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)gpu_dev.notify_base, notify_phys, 3,
//                    PAGE_PRESENT | PAGE_WRITABLE);
//     gpu_dev.notify_multiplier = notify_cfg->length;

//     // Map ISR region
//     uint8_t isr_bar = isr_cfg->bar;
//     LOG_DEBUG("VIRTIO", "ISR BAR: %d", isr_bar);

//     if (isr_bar >= 6 || pci_device->bar[isr_bar].iospace)
//     {
//         LOG_ERROR("VIRTIO", "Invalid ISR BAR: %d", isr_bar);
//         return;
//     }

//     uintptr_t isr_phys =
//         (pci_device->bar[3].address << 32 | (pci_device->bar[2].address & ~0xF)) + isr_cfg->offset;
//     // uintptr_t isr_phys = pci_device->bar[isr_bar].address + isr_cfg->offset;
//     gpu_dev.isr = (volatile uint8_t *)pDMAalloc(3);
//     if (!gpu_dev.isr)
//     {
//         LOG_ERROR("VIRTIO", "Failed to allocate memory for ISR region");
//         return;
//     }
//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)gpu_dev.isr, isr_phys, 3,
//                    PAGE_PRESENT | PAGE_WRITABLE);

//     paging_reload(paging_get_highest_page_map());

//     LOG_INFO("VIRTIO", "Mapped VirtIO structures: common_cfg=%x, notify_base=%x, isr=%x",
//              gpu_dev.common_cfg, gpu_dev.notify_base, gpu_dev.isr);

//     // Reset device
//     gpu_dev.common_cfg->device_status = 0;
//     LOG_DEBUG("VIRTIO", "Device reset");

//     // Acknowledge device
//     gpu_dev.common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
//     LOG_DEBUG("VIRTIO", "Device acknowledged");

//     // Driver ready
//     gpu_dev.common_cfg->device_status |= VIRTIO_STATUS_DRIVER;
//     LOG_DEBUG("VIRTIO", "Driver ready");

//     // Feature negotiation
//     gpu_dev.common_cfg->device_feature_select = 0;
//     uint32_t device_features                  = gpu_dev.common_cfg->device_feature;
//     LOG_DEBUG("VIRTIO", "Device features: 0x%x", device_features);

//     // Accept only essential features for now
//     gpu_dev.common_cfg->driver_feature_select = 0;
//     gpu_dev.common_cfg->driver_feature        = device_features & 0x1; // Only basic feature bit
//     gpu_dev.common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;

//     // Verify features
//     if (!(gpu_dev.common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK))
//     {
//         LOG_ERROR("VIRTIO", "Feature negotiation failed");
//         return;
//     }
//     LOG_DEBUG("VIRTIO", "Features OK");

//     // Setup control queue
//     gpu_dev.common_cfg->queue_select = 0;
//     uint16_t queue_size              = gpu_dev.common_cfg->queue_size;
//     if (queue_size == 0 || queue_size > VIRTIO_GPU_QUEUE_SIZE)
//     {
//         queue_size = VIRTIO_GPU_QUEUE_SIZE;
//     }
//     LOG_DEBUG("VIRTIO", "Queue size: %d", queue_size);

//     // Allocate and initialize virtqueue
//     size_t vq_total_size = queue_size * sizeof(struct virtq_desc) + sizeof(struct virtq_avail) +
//                            sizeof(uint16_t) * queue_size + sizeof(struct virtq_used) +
//                            sizeof(struct virtq_used_elem) * queue_size;
//     vq_total_size = ALIGN_UP(vq_total_size, VIRTIO_GPU_QUEUE_ALIGN);

//     void *vq_mem = pDMAalloc(1 + (vq_total_size + BLOCK_SIZE - 1) / BLOCK_SIZE);
//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)vq_mem, (uintptr_t)vq_mem,
//                    1 + (vq_total_size + BLOCK_SIZE - 1) / BLOCK_SIZE, PAGE_PRESENT | PAGE_WRITABLE);
//     paging_get_highest_page_map();
//     if (!vq_mem)
//     {
//         LOG_ERROR("VIRTIO", "Failed to allocate virtqueue memory");
//         return;
//     }
//     memset(vq_mem, 0, vq_total_size);

//     uintptr_t vq_phys = (uintptr_t)vq_mem;
//     virtq_init(&gpu_dev.controlq, vq_mem, queue_size, vq_phys);

//     // Configure queue with physical addresses
//     gpu_dev.common_cfg->queue_desc  = vq_phys;
//     gpu_dev.common_cfg->queue_avail = vq_phys + queue_size * sizeof(struct virtq_desc);

//     // Calculate used ring address properly
//     uintptr_t used_offset          = (uintptr_t)gpu_dev.controlq.used - (uintptr_t)vq_mem;
//     gpu_dev.common_cfg->queue_used = vq_phys + used_offset;

//     gpu_dev.notify_offset = gpu_dev.common_cfg->queue_notify_off * gpu_dev.notify_multiplier;

//     LOG_DEBUG("VIRTIO",
//               "Virtqueue configured: desc=0x%x, avail=0x%x, used=0x%x, notify_offset=0x%x",
//               gpu_dev.common_cfg->queue_desc, gpu_dev.common_cfg->queue_avail,
//               gpu_dev.common_cfg->queue_used, gpu_dev.notify_offset);

//     // Enable queue
//     gpu_dev.common_cfg->queue_enable = 1;
//     LOG_DEBUG("VIRTIO", "Queue enabled");

//     // Driver OK
//     gpu_dev.common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
//     LOG_DEBUG("VIRTIO", "Driver OK");

//     // FIX: Add delay to ensure device is ready
//     for (volatile int i = 0; i < 100000; i++)
//         ;

//     gpu_dev.initialized = true;

//     LOG_INFO("VIRTIO", "VirtIO GPU initialized successfully");

//     // Allocate buffers untuk test
//     struct virtio_gpu_ctrl_hdr          *test_cmd = (struct virtio_gpu_ctrl_hdr *)pDMAalloc(1);
//     struct virtio_gpu_resp_display_info *test_resp =
//         (struct virtio_gpu_resp_display_info *)pDMAalloc(1);

//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)test_cmd, (uintptr_t)test_cmd, 1,
//                    PAGE_PRESENT | PAGE_WRITABLE);

//     vxMultipleMmap(paging_get_highest_page_map(), (uintptr_t)test_resp, (uintptr_t)test_resp, 1,
//                    PAGE_PRESENT | PAGE_WRITABLE);

//     paging_reload(paging_get_highest_page_map());

//     if (!test_cmd || !test_resp)
//     {
//         LOG_ERROR("VIRTIO", "Failed to allocate test buffers");
//         return;
//     }

//     // Test communication
//     memset(test_cmd, 0, sizeof(*test_cmd));
//     test_cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

//     memset(test_resp, 0, sizeof(*test_resp));

//     LOG_INFO("VIRTIO", "Test buffers allocated: cmd=%p, resp=%p", test_cmd, test_resp);

//     // Beri waktu untuk device stabil
//     for (volatile int i = 0; i < 1000000; i++)
//         ;

//     // Coba kirim command
//     int result = virtio_gpu_send_command(&gpu_dev, test_cmd, sizeof(*test_cmd), test_resp,
//                                          sizeof(*test_resp));

//     if (result == 0)
//     {
//         LOG_INFO("VIRTIO", "GPU communication test PASSED");

//         if (test_resp->hdr.type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
//         {
//             LOG_INFO("VIRTIO", "Display info received successfully");
//             int enabled_count = 0;
//             for (int i = 0; i < 16; i++)
//             {
//                 if (test_resp->pmodes[i].enabled)
//                 {
//                     LOG_INFO("VIRTIO", "Scanout %d: %dx%d at (%d,%d)", i,
//                              test_resp->pmodes[i].rect.width, test_resp->pmodes[i].rect.height,
//                              test_resp->pmodes[i].rect.x, test_resp->pmodes[i].rect.y);
//                     enabled_count++;
//                 }
//             }
//             if (enabled_count == 0)
//             {
//                 LOG_WARN("VIRTIO", "No enabled display modes found");
//             }
//         }
//         else
//         {
//             LOG_ERROR("VIRTIO", "Unexpected response type: 0x%x", test_resp->hdr.type);
//         }
//     }
//     else
//     {
//         LOG_ERROR("VIRTIO", "GPU communication test FAILED");
//     }

//     gpu_dev.initialized = true;

//     // Create a test resource
//     if (virtio_gpu_create_resource(&gpu_dev, 1, 1366, 768) == 0)
//     {
//         LOG_INFO("VIRTIO", "Test resource created successfully");
//     }
//     else
//     {
//         LOG_WARN("VIRTIO", "Failed to create test resource");
//     }
// }

// void
// virtio_irq()
// {
//     irq_trigered = true;
// }