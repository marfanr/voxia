#ifndef GRAPHIC_IOCTL_H
#define GRAPHIC_IOCTL_H

#include <stdint.h>
#include <stddef.h>

// Copied from voxia2 kernel graphic.h
typedef enum : uint32_t {
	GRAPHIC_FORMAT_NONE = 0,
	GRAPHIC_FORMAT_R8G8B8A8_UNORM,
	GRAPHIC_FORMAT_B8G8R8A8_UNORM,
	GRAPHIC_FORMAT_B8G8R8X8_UNORM,
	GRAPHIC_FORMAT_A8R8G8B8_UNORM,
	GRAPHIC_FORMAT_X8R8G8B8_UNORM,
	GRAPHIC_FORMAT_B5G5R5A1_UNORM,
	GRAPHIC_FORMAT_B4G4R4A4_UNORM,
	GRAPHIC_FORMAT_B5G6R5_UNORM,
	GRAPHIC_FORMAT_R10G10B10A2_UNORM,
	GRAPHIC_FORMAT_L8_UNORM,
	GRAPHIC_FORMAT_A8_UNORM,
	GRAPHIC_FORMAT_I8_UNORM,
	GRAPHIC_FORMAT_L8A8_UNORM,
	GRAPHIC_FORMAT_L16_UNORM,
	GRAPHIC_FORMAT_UYVY,
	GRAPHIC_FORMAT_YUYV,
	GRAPHIC_FORMAT_Z16_UNORM,
	GRAPHIC_FORMAT_Z32_UNORM,
	GRAPHIC_FORMAT_Z32_FLOAT,
	GRAPHIC_FORMAT_Z24_UNORM_S8_UINT,
	GRAPHIC_FORMAT_S8_UINT_Z24_UNORM,
	GRAPHIC_FORMAT_Z24X8_UNORM,
	GRAPHIC_FORMAT_X8Z24_UNORM,
	GRAPHIC_FORMAT_S8_UINT,
	GRAPHIC_FORMAT_R64_FLOAT,
	GRAPHIC_FORMAT_R64G64_FLOAT,
	GRAPHIC_FORMAT_R64G64B64_FLOAT,
	GRAPHIC_FORMAT_R64G64B64A64_FLOAT,
	GRAPHIC_FORMAT_R32_FLOAT,
	GRAPHIC_FORMAT_R32G32_FLOAT,
	GRAPHIC_FORMAT_R32G32B32_FLOAT,
	GRAPHIC_FORMAT_R32G32B32A32_FLOAT,

	GRAPHIC_FORMAT_RGBA8 = GRAPHIC_FORMAT_R8G8B8A8_UNORM,
	GRAPHIC_FORMAT_BGRA8 = GRAPHIC_FORMAT_B8G8R8A8_UNORM,
	GRAPHIC_FORMAT_RGB565 = GRAPHIC_FORMAT_B5G6R5_UNORM,
} graphic_format_t;

enum graphic_ioctl_cmd {
    GRAPHIC_IOCTL_GET_DISPLAY_INFO = 0,
    GRAPHIC_IOCTL_USE_3D,
    GRAPHIC_IOCTL_CREATE_RESOURCE,
    GRAPHIC_IOCTL_DESTROY_RESOURCE,
    GRAPHIC_IOCTL_SET_SCANOUT,
    GRAPHIC_IOCTL_CREATE_CONTEXT,
    GRAPHIC_IOCTL_DESTROY_CONTEXT,
    GRAPHIC_IOCTL_BIND_RESOURCE,
    GRAPHIC_IOCTL_UNBIND_RESOURCE,
    GRAPHIC_IOCTL_ATTACH_BACKING,
    GRAPHIC_IOCTL_SUBMIT,
    GRAPHIC_IOCTL_TRANSFER,
    GRAPHIC_IOCTL_TRANSFER2,
    GRAPHIC_IOCTL_RESOURCE_FLUSH,
    GRAPHIC_IOCTL_TRANSFER_FROM,
    GRAPHIC_IOCTL_UPDATE_CURSOR,
    GRAPHIC_IOCTL_MOVE_CURSOR,
};

struct graphic_ioctl_use_3d_cmd {
    int use_3d;
};

struct graphic_context_desc {
    uint32_t id;
    char* name;
    uint32_t nlen;
    uint32_t context_init;
} __attribute__((aligned(64)));

struct graphic_ioctl_create_context_cmd {
    struct graphic_context_desc desc;
    uint32_t context_id;
};

typedef enum : uint32_t {
    GRAPHIC_RESOURCE_BUFFER,
    GRAPHIC_RESOURCE_TEXTURE_1D,
    GRAPHIC_RESOURCE_TEXTURE_2D,
    GRAPHIC_RESOURCE_TEXTURE_3D
} graphic_resource_type_t;

typedef enum : uint32_t {
    GRAPHIC_BIND_RENDER_TARGET = 1 << 0,
    GRAPHIC_BIND_DEPTH = 1 << 1,
    GRAPHIC_BIND_VERTEX = 1 << 2,
    GRAPHIC_BIND_INDEX = 1 << 3,
    GRAPHIC_BIND_TEXTURE = 1 << 4,
    GRAPHIC_BIND_SCANOUT = 1 << 5,
} graphic_resource_bind_t;

struct graphic_resource_desc {
    uint32_t id;
    graphic_resource_type_t type;

    graphic_format_t format;
    graphic_resource_bind_t bind;
    uint32_t flags;

    uint32_t ctx_id;
    void* ctx; // changed from struct graphic_context* to void* to avoid missing type

    uint32_t width;
    uint32_t height;
    uint32_t depth;

    uint32_t array_size;
    uint32_t mip_levels;
    uint32_t sample_counts;
};

struct graphic_ioctl_create_resource_cmd {
    struct graphic_resource_desc desc;
    uint32_t resource_id;
};

struct graphic_ioctl_attach_backing_cmd {
    uint32_t resource_id;
    uint64_t vaddr;
    size_t size;
};

struct graphic_ioctl_bind_resource_cmd {
    uint32_t context_id;
    uint32_t resource_id;
};

struct graphic_ioctl_set_scanout_cmd {
    uint32_t scanout_id;
    uint32_t resource_id;
};

struct graphic_ioctl_submit_cmd {
    uint32_t context_id;
    void* commands;
    size_t size;
};

struct graphic_ioctl_resource_flush_cmd {
    uint32_t resource_id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct graphic_transfer_cmd {
    uint32_t context_id;
    uint32_t resource_id;
    uint64_t offset;
    uint32_t level;
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
    uint32_t h;
    uint32_t d;
    void* data;
};

struct graphic_ioctl_update_cursor_cmd {
    uint32_t scanout_id;
    uint32_t resource_id;
    uint32_t hot_x;
    uint32_t hot_y;
};

struct graphic_ioctl_move_cursor_cmd {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
};

#endif
