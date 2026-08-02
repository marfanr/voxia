#ifndef __USB_HID__HID_HPP__
#define __USB_HID__HID_HPP__

#include "ioforge/ioforge_virtio.h"
#include "ioforge/ioforge_virtio.hpp"
#include <procc/thread.h>

class VirtioGpu : public IoForgeVirtio {
      public:
	VirtioGpu();
	void load() override;
	void unload() override;
	static VirtioGpu* getInstance();

	//   protected:
	void setup();
	ioforge_virtio_device* dev_;
	int virtio_gpu_send_command(void* cmd, uint32_t cmd_size, void* resp,
	                            uint32_t resp_size, int queue_idx = 0);
	int virtio_gpu_get_display_info();
	int virtio_gpu_create_resource(uint32_t resource_id, uint32_t width,
	                               uint32_t height);
	int virtio_gpu_unref_resource(uint32_t resource_id);
	int virtio_gpu_set_scanout(uint32_t scanout_id, uint32_t resource_id,
	                           uint32_t x, uint32_t y, uint32_t width,
	                           uint32_t height);
	int virtio_gpu_transfer_to_host_2d(uint64_t offset,
	                                   uint32_t resource_id, uint32_t x,
	                                   uint32_t y, uint32_t width,
	                                   uint32_t height);
	int virtio_gpu_resource_flush(uint32_t resource_id, uint32_t x,
	                              uint32_t y, uint32_t width,
	                              uint32_t height);
	int virtio_gpu_attach_backing(uint32_t resource_id, uint32_t nr_entries,
	                              struct virtio_gpu_mem_entry* entries);
	int virtio_gpu_resource_create_blob(uint32_t ctx_id,
	                                    uint32_t resource_id,
	                                    uint32_t blob_mem,
	                                    uint32_t blob_flags,
	                                    uint64_t blob_id, uint64_t size);

	// 3D
	int virtio_gpu_ctx_create(uint32_t ctx_id, uint32_t nlen,
	                          uint32_t context_init, char* debug_name);
	int virtio_gpu_ctx_destroy(uint32_t ctx_id);
	int virtio_gpu_resource_create_3d(uint32_t ctx_id, uint32_t resource_id,
	                                  uint32_t target, uint32_t format,
	                                  uint32_t bind, uint32_t width,
	                                  uint32_t height, uint32_t depth,
	                                  uint32_t array_size,
	                                  uint32_t last_level,
	                                  uint32_t nr_samples, uint32_t flags);
	int virtio_gpu_ctx_attach_resource(uint32_t ctx_id,
	                                   uint32_t resource_id);
	int virtio_gpu_submit_3d(uint32_t ctx_id, void* cmd_buf,
	                         uint32_t cmd_buf_size);
	int virtio_gpu_transfer_to_host_3d(
	    uint32_t ctx_id, uint32_t resource_id, uint64_t offset,
	    uint32_t level, uint32_t x, uint32_t y, uint32_t z, uint32_t w,
	    uint32_t h, uint32_t d, uint32_t stride, uint32_t layer_stride);
	int virtio_gpu_transfer_from_host_3d(
	    uint32_t ctx_id, uint32_t resource_id, uint64_t offset,
	    uint32_t level, uint32_t x, uint32_t y, uint32_t z, uint32_t w,
	    uint32_t h, uint32_t d, uint32_t stride, uint32_t layer_stride);

	int virtio_gpu_update_cursor(uint32_t resource_id, uint32_t scanout_id,
	                             uint32_t x, uint32_t y, uint32_t hot_x,
	                             uint32_t hot_y);
	int virtio_gpu_move_cursor(uint32_t scanout_id, uint32_t x, uint32_t y);

	uint32_t cursor_res_id_;
	uint32_t cursor_x_;
	uint32_t cursor_y_;
	uint32_t cursor_hot_x_ = 0;
	uint32_t cursor_hot_y_ = 0;

      private:
	uint16_t virtq_alloc_desc(struct virtio_gpu_queue* vq);
	void virtq_free_desc(struct virtio_gpu_queue* vq, uint16_t desc_idx);
	void virtq_init(struct virtio_gpu_queue* vq, void* vq_mem,
	                uint16_t queue_size, uintptr_t phys_addr);
	int virtq_add_buf(struct virtio_gpu_queue* vq, void** buffers,
	                  uint32_t* lengths, uint16_t num_out, uint16_t num_in,
	                  uint16_t* head_out);
	void virtq_kick(uint16_t queue_index);
	int virtq_get_used_elem(struct virtio_gpu_queue* vq, uint16_t* id,
	                        uint32_t* len);

	bool initialized_;
	uintptr_t notify_offset_[4];
	uint32_t notify_multiplier_;
	uintptr_t notify_base_;

	static void fireHandler();

	uint32_t max_scanouts;

	// will be removed
	void test();
	void venus_triangle_test();

	static struct virtio_gpu_queue control_queue_;
	static struct virtio_gpu_queue cursor_queue_;
	static spinlock_t controlq_lock_;
	static spinlock_t cursorq_lock_;
	thread_t* waiting_threads[256];
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
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB 0x010C
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO 0x108
#define VIRTIO_GPU_CMD_GET_CAPSET 0x109

#define VIRTIO_GPU_CMD_CTX_CREATE 0X0200
#define VIRTIO_GPU_CMD_CTX_DESTROY 0x0201
#define VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE 0x0202
#define VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE 0x0203
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D 0x0204
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D 0x0205
#define VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D 0x0206
#define VIRTIO_GPU_CMD_SUBMIT_3D 0x0207

#define VIRTIO_GPU_CMD_UPDATE_CURSOR 0x0300
#define VIRTIO_GPU_CMD_MOVE_CURSOR 0x0301

// --- VirGL Object Types (sesuai virgl_protocol.h) ---
#define VIRGL_OBJECT_BLEND 1
#define VIRGL_OBJECT_RASTERIZER 2
#define VIRGL_OBJECT_DSA 3
#define VIRGL_OBJECT_SHADER 4
#define VIRGL_OBJECT_VERTEX_ELEMENTS 5
#define VIRGL_OBJECT_SAMPLER_STATE 6
#define VIRGL_OBJECT_SAMPLER_VIEW 7
#define VIRGL_OBJECT_SURFACE 8

#define PIPE_BUFFER 0
#define PIPE_TEXTURE_1D 1
#define PIPE_TEXTURE_2D 2
#define PIPE_TEXTURE_3D 3
#define PIPE_TEXTURE_CUBE 4
#define PIPE_TEXTURE_RECT 5
#define PIPE_TEXTURE_1D_ARRAY 6
#define PIPE_TEXTURE_2D_ARRAY 7
#define PIPE_TEXTURE_CUBE_ARRAY 8

// Sumber: enum virgl_formats` di `virgl_hw.h` (virglrenderer project)
// Referensi lengkap:
// https://gitlab.freedesktop.org/virgl/virglrenderer/-/blob/master/src/virgl_hw.h
#define PIPE_FORMAT_NONE 0
#define PIPE_FORMAT_B8G8R8A8_UNORM 1
#define PIPE_FORMAT_B8G8R8X8_UNORM 2
#define PIPE_FORMAT_A8R8G8B8_UNORM 3
#define PIPE_FORMAT_X8R8G8B8_UNORM 4
#define PIPE_FORMAT_B5G5R5A1_UNORM 5
#define PIPE_FORMAT_B4G4R4A4_UNORM 6
#define PIPE_FORMAT_B5G6R5_UNORM 7
#define PIPE_FORMAT_R10G10B10A2_UNORM 8
#define PIPE_FORMAT_L8_UNORM 9
#define PIPE_FORMAT_A8_UNORM 10
#define PIPE_FORMAT_I8_UNORM 11
#define PIPE_FORMAT_L8A8_UNORM 12
#define PIPE_FORMAT_L16_UNORM 13
#define PIPE_FORMAT_UYVY 14
#define PIPE_FORMAT_YUYV 15
#define PIPE_FORMAT_Z16_UNORM 16
#define PIPE_FORMAT_Z32_UNORM 17
#define PIPE_FORMAT_Z32_FLOAT 18
#define PIPE_FORMAT_Z24_UNORM_S8_UINT 19
#define PIPE_FORMAT_S8_UINT_Z24_UNORM 20
#define PIPE_FORMAT_Z24X8_UNORM 21
#define PIPE_FORMAT_X8Z24_UNORM 22
#define PIPE_FORMAT_S8_UINT 23
#define PIPE_FORMAT_R64_FLOAT 24
#define PIPE_FORMAT_R64G64_FLOAT 25
#define PIPE_FORMAT_R64G64B64_FLOAT 26
#define PIPE_FORMAT_R64G64B64A64_FLOAT 27
#define PIPE_FORMAT_R32_FLOAT 28
#define PIPE_FORMAT_R32G32_FLOAT 29
#define PIPE_FORMAT_R32G32B32_FLOAT 30
#define PIPE_FORMAT_R32G32B32A32_FLOAT 31
#define PIPE_FORMAT_R8_UNORM 64
#define PIPE_FORMAT_R8G8_UNORM 65
#define PIPE_FORMAT_R8G8B8_UNORM 66
#define PIPE_FORMAT_R8G8B8A8_UNORM 67
#define PIPE_FORMAT_R8G8B8X8_UNORM 68

#define PIPE_BIND_DEPTH_STENCIL (1 << 0)
#define PIPE_BIND_RENDER_TARGET (1 << 1)
#define PIPE_BIND_SAMPLER_VIEW (1 << 2)
#define PIPE_BIND_VERTEX_BUFFER (1 << 3)
#define PIPE_BIND_INDEX_BUFFER (1 << 4)
#define PIPE_BIND_CONSTANT_BUFFER (1 << 5)
#define PIPE_BIND_DISPLAY_TARGET (1 << 6)
#define PIPE_BIND_SCANOUT (1 << 7)

#define PIPE_RESOURCE_FLAG_MAP_PERSISTENT (1 << 0)
#define PIPE_RESOURCE_FLAG_MAP_COHERENT (1 << 1)
#define PIPE_RESOURCE_FLAG_TEXTURING_MORE_LIKELY (1 << 2)
#define PIPE_RESOURCE_FLAG_SPARSE (1 << 3)
#define PIPE_RESOURCE_FLAG_SINGLE_THREAD_USE (1 << 4)
#define PIPE_RESOURCE_FLAG_ENCRYPTED (1 << 5)
#define PIPE_RESOURCE_FLAG_DONT_OVER_ALLOCATE (1 << 6)
#define PIPE_RESOURCE_FLAG_DONT_MAP_DIRECTLY                                   \
	(1 << 7) /* for small visible VRAM */
#define PIPE_RESOURCE_FLAG_UNMAPPABLE                                          \
	(1 << 8) /* implies staging transfers due to VK interop */
#define PIPE_RESOURCE_FLAG_FIXED_ADDRESS                                       \
	(1 << 9) /* virtual memory address never changes */
#define PIPE_RESOURCE_FLAG_FRONTEND_VM                                         \
	(1 << 10) /* the frontend assigns addresses */
#define PIPE_RESOURCE_FLAG_DRV_PRIV (1 << 11) /* driver/winsys private */
#define PIPE_RESOURCE_FLAG_FRONTEND_PRIV (1 << 24) /* gallium frontend private */

// VirGL Command IDs — sesuai enum virgl_context_cmd di virgl_protocol.h
#define VIRGL_CCMD_NOP 0
#define VIRGL_CCMD_CREATE_OBJECT 1
#define VIRGL_CCMD_BIND_OBJECT 2
#define VIRGL_CCMD_DESTROY_OBJECT 3
#define VIRGL_CCMD_SET_VIEWPORT_STATE 4
#define VIRGL_CCMD_SET_FRAMEBUFFER_STATE 5
#define VIRGL_CCMD_SET_VERTEX_BUFFERS 6
#define VIRGL_CCMD_CLEAR 7
#define VIRGL_CCMD_DRAW_VBO 8
#define VIRGL_CCMD_SET_SAMPLER_VIEWS 10
#define VIRGL_CCMD_SET_CONSTANT_BUFFER 12
#define VIRGL_CCMD_BIND_SAMPLER_STATES 18
#define VIRGL_CCMD_BIND_SHADER 31

#define VIRGL_CMD0(cmd, obj, len) ((cmd) | ((obj) << 8) | ((len) << 16))

#define PIPE_PRIM_POINTS 0
#define PIPE_PRIM_LINES 1
#define PIPE_PRIM_LINE_LOOP 2
#define PIPE_PRIM_LINE_STRIP 3
#define PIPE_PRIM_TRIANGLES 4
#define PIPE_PRIM_TRIANGLE_STRIP 5
#define PIPE_PRIM_TRIANGLE_FAN 6

#define PIPE_USAGE_DEFAULT 0
#define PIPE_USAGE_STREAM 1
#define PIPE_USAGE_DYNAMIC 2
#define PIPE_USAGE_STATIC 3

#define PIPE_TRANSFER_READ (1 << 0)
#define PIPE_TRANSFER_WRITE (1 << 1)
#define PIPE_TRANSFER_MAP_DIRECTLY (1 << 2)
#define PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE (1 << 3)

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

// feature
#define VIRTIO_GPU_F_VIRGL (1 << 0)
#define VIRTIO_GPU_F_EDID (1 << 1)

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

struct virtio_gpu_cursor_pos {
	uint32_t scanout_id;
	uint32_t x;
	uint32_t y;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_update_cursor {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_cursor_pos pos;
	uint32_t resource_id;
	uint32_t hot_x;
	uint32_t hot_y;
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

struct virtio_gpu_mem_entry {
	uint64_t addr;
	uint32_t length;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t nr_entries;
	struct virtio_gpu_mem_entry entries[];
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

struct virtio_gpu_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
} __attribute__((packed));

struct virtio_gpu_transfer_host_3d {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_box box;
	uint64_t offset;
	uint32_t resource_id;
	uint32_t level;
	uint32_t stride;
	uint32_t layer_stride;
} __attribute__((packed));

struct virtio_gpu_resource_unref {
	struct virtio_gpu_ctrl_hdr hdr;
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

// virtgl
#define VIRTIO_GPU_CONTEXT_INIT_CAPSET_ID_MASK 0x000000ff;
struct virtio_gpu_ctx_create {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t nlen;
	uint32_t context_init;
	char debug_name[64];
};

struct virtio_gpu_ctx_destroy {
	struct virtio_gpu_ctrl_hdr hdr;
} __attribute__((packed));

struct virtio_gpu_resource_create_3d {
	struct virtio_gpu_ctrl_hdr hdr;

	uint32_t resource_id;
	uint32_t target;
	uint32_t format;
	uint32_t bind;

	uint32_t width;
	uint32_t height;
	uint32_t depth;

	uint32_t array_size;
	uint32_t last_level;

	uint32_t nr_samples;
	uint32_t flags;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_ctx_resource {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_cmd_submit {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t size;
	uint32_t padding;
} __attribute__((packed));

#define VIRTIO_GPU_BLOB_MEM_GUEST 0x0001
#define VIRTIO_GPU_BLOB_MEM_HOST3D 0x0002
#define VIRTIO_GPU_BLOB_MEM_HOST3D_GUEST 0x0003
#define VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE 0x0001
#define VIRTIO_GPU_BLOB_FLAG_USE_SHAREABLE 0x0002
#define VIRTIO_GPU_BLOB_FLAG_USE_CROSS_DEVICE 0x0004

struct virtio_gpu_resource_create_blob {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t blob_mem;
	uint32_t blob_flags;
	uint32_t nr_entries;
	uint64_t blob_id;
	uint64_t size;
} __attribute__((packed));

struct virtio_gpu_get_capset_info {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t capset_index;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_capset_info_response {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t capset_id;
	uint32_t capset_max_version;
	uint32_t capset_max_size;
	uint32_t padding;
} __attribute__((packed));

#endif
