#ifndef __USB_HID__HID_HPP__
#define __USB_HID__HID_HPP__

#include "ioforge/ioforge_virtio.h"
#include "ioforge/ioforge_virtio.hpp"

class VirtioGpu : public IoForgeVirtio {
      public:
	VirtioGpu();
	void load() override;
	void unload() override;
	static VirtioGpu* getInstance();

      protected:
	void setup();
	ioforge_virtio_device* dev_;

      private:
	uint16_t virtq_alloc_desc(struct virtio_gpu_queue* vq);
	void virtq_free_desc(struct virtio_gpu_queue* vq, uint16_t desc_idx);
	void virtq_init(struct virtio_gpu_queue* vq, void* vq_mem,
			uint16_t queue_size, uintptr_t phys_addr);
	int virtq_add_buf(struct virtio_gpu_queue* vq, void** buffers,
			  uint32_t* lengths, uint16_t num_out, uint16_t num_in,
			  uint16_t* head_out);
	void virtq_kick(struct virtio_gpu_device* dev, uint16_t queue_index);
	int virtq_get_used_elem(struct virtio_gpu_queue* vq, uint16_t* id,
				uint32_t* len);
	int virtio_gpu_send_command(struct virtio_gpu_device* dev, void* cmd,
				    uint32_t cmd_size, void* resp,
				    uint32_t resp_size);
	int virtio_gpu_get_display_info(struct virtio_gpu_device* dev);
	int virtio_gpu_create_resource(struct virtio_gpu_device* dev,
				       uint32_t resource_id, uint32_t width,
				       uint32_t height);

	bool initialized_;
	uintptr_t notify_offset_;
	uint32_t notify_multiplier_;
	uintptr_t notify_base_;

	static void fireHandler();

	static struct virtio_gpu_queue control_queue_;
};

// VirtIO GPU Command Types
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107

// Response Types
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101

// Formats
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM 3

// VirtIO Device Status bits
#define VIRTIO_STATUS_ACKNOWLEDGE (1 << 0)
#define VIRTIO_STATUS_DRIVER (1 << 1)
#define VIRTIO_STATUS_DRIVER_OK (1 << 2)
#define VIRTIO_STATUS_FEATURES_OK (1 << 3)
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET (1 << 6)
#define VIRTIO_STATUS_FAILED (1 << 7)

// Descriptor Flags
#define VIRTQ_DESC_F_NEXT (1 << 0)
#define VIRTQ_DESC_F_WRITE (1 << 1)
#define VIRTQ_DESC_F_INDIRECT (1 << 2)

// ISR Status bits
#define VIRTIO_ISR_QUEUE_INT (1 << 0)
#define VIRTIO_ISR_DEV_CFG_INT (1 << 1)

// ---------------- VirtIO PCI Common Config (BAR0) ----------------
struct virtio_pci_common_cfg {
	uint32_t device_feature_select;
	uint32_t device_feature;
	uint32_t driver_feature_select;
	uint32_t driver_feature;
	uint16_t msix_config;
	uint16_t num_queues;
	uint8_t device_status;
	uint8_t config_generation;

	uint16_t queue_select;
	uint16_t queue_size;
	uint16_t queue_msix_vector;
	uint16_t queue_enable;
	uint16_t queue_notify_off;
	uint64_t queue_desc;
	uint64_t queue_avail;
	uint64_t queue_used;
} __attribute__((packed));

// ---------------- Virtqueue Structures ----------------
#define VIRTIO_GPU_QUEUE_SIZE 64
#define VIRTIO_GPU_QUEUE_ALIGN 4096

struct virtq_desc {
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
} __attribute__((packed));

struct virtq_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[VIRTIO_GPU_QUEUE_SIZE];
	uint16_t used_event;
} __attribute__((packed));

struct virtq_used_elem {
	uint32_t id;
	uint32_t len;
} __attribute__((packed));

struct virtq_used {
	uint16_t flags;
	uint16_t idx;
	struct virtq_used_elem ring[VIRTIO_GPU_QUEUE_SIZE];
	uint16_t avail_event;
} __attribute__((packed));

struct virtio_gpu_queue {
	struct virtq_desc* desc;
	struct virtq_avail* avail;
	struct virtq_used* used;
	uint16_t queue_size;
	uint16_t free_head;
	uint16_t num_free;
	uint16_t last_used_idx;
	uintptr_t phys_addr;
};

// ---------------- VirtIO GPU Structures ----------------
struct virtio_gpu_ctrl_hdr {
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	uint32_t ctx_id;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_display_one {
		struct virtio_gpu_rect rect;
		uint32_t enabled;
		uint32_t flags;
	} pmodes[16];
} __attribute__((packed));

struct virtio_gpu_resource_create_2d {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t nr_entries;
} __attribute__((packed));

struct virtio_gpu_mem_entry {
	uint64_t addr;
	uint32_t length;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_set_scanout {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect rect;
	uint32_t scanout_id;
	uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_transfer_to_host_2d {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect rect;
	uint64_t offset;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect rect;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

// Driver state
struct virtio_gpu_device {
	struct virtio_pci_common_cfg* common_cfg;
	volatile uint32_t* notify_base;
	volatile uint8_t* isr;
	uintptr_t notify_offset;
	uintptr_t notify_multiplier;
	struct virtio_gpu_queue controlq;
	uint32_t features;
	bool initialized;
};

#endif //__USB_HID__HID_HPP__