// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Mohammad Arfan

#ifndef __HAL__GRAPHIC_H__
#define __HAL__GRAPHIC_H__

#include "spinlock.h"
#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint8_t b, g, r, a;
} pixel_t;

// new API
/* GPU GRAPHIC API */
struct graphic_resource;
struct graphic_scanout {
	uint16_t id;
	uint8_t flag;

	int width;
	int height;
	int x;
	int y;

	struct graphic_resource* res;
	struct graphic_scanout* next;

} __attribute__((aligned(64)));

struct graphic_device;
struct graphic_context;

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
	GRAPHIC_BIND_2D = 1 << 6,
} graphic_resource_bind_t;

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

struct graphic_resource {
	uint16_t id;
	graphic_resource_type_t type;
	graphic_format_t format;
	graphic_resource_bind_t bind;

	uintptr_t vaddr; // original vaddr from DMAAlloc (use for DMAFree)
	uintptr_t paddr; // original paddr from DMAAlloc (use for DMAFree)
	size_t size;     // total bytes handed to DMAAlloc
	int x;
	int y;
	int width;
	int height;
	int depth;

	struct graphic_context* ctx;

	void* data;
	struct graphic_resource* next;
} __attribute__((aligned(64)));

struct graphic_resource_desc {
	uint32_t id;
	graphic_resource_type_t type;

	graphic_format_t format;
	graphic_resource_bind_t bind;
	uint32_t flags;

	uint32_t ctx_id;
	struct graphic_context* ctx;

	uint32_t width;
	uint32_t height;
	uint32_t depth;

	uint32_t array_size;
	uint32_t mip_levels;
	uint32_t sample_counts;
};

struct graphic_context_desc {
	uint32_t id;
	char* name;
	uint32_t nlen;
	uint32_t context_init;
} __attribute__((aligned(64)));

struct graphic_device_ops {

	int (*resource_create)(struct graphic_device*,
	                       struct graphic_resource_desc*,
	                       struct graphic_resource**);
	int (*create_context)(struct graphic_device*,
	                      struct graphic_context_desc*,
	                      struct graphic_context**);
	int (*resource_attach_backing)(struct graphic_device*,
	                               struct graphic_resource*);
	int (*resource_destroy)(struct graphic_device*,
	                        struct graphic_resource*);
	int (*scanout_set)(struct graphic_device*, struct graphic_scanout*,
	                   struct graphic_resource*);
	int (*destroy_context)(struct graphic_device*, struct graphic_context*);
	int (*resource_flush)(struct graphic_device*, struct graphic_resource*,
	                      uint32_t x, uint32_t y, uint32_t width,
	                      uint32_t height);
	int (*update_cursor)(struct graphic_device*, struct graphic_scanout*,
	                     struct graphic_resource*, uint32_t hot_x,
	                     uint32_t hot_y);
	int (*move_cursor)(struct graphic_device*, struct graphic_scanout*,
	                   uint32_t x, uint32_t y);
};

struct graphic_device {
	uint32_t id;
	spinlock_t lock;

	boolean_t is_3d;
	boolean_t is_enable;

	size_t scanout_count;
	struct graphic_scanout* scanout_list;

	struct graphic_context* context_list;
	struct graphic_resource* resource_list;

	struct graphic_device_ops* ops;

	struct graphic_device* next;

} __attribute__((aligned(64)));

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

struct graphic_transfer2_cmd {
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
	uint32_t stride;
	uint32_t layer_stride;
};

struct graphic_context;
struct graphic_context_ops {
	int (*bind_resource)(struct graphic_context*, struct graphic_resource*);

	int (*unbind_resource)(struct graphic_context*,
	                       struct graphic_resource);

	int (*submit)(struct graphic_context*, const void* commands,
	              size_t size);

	int (*transfer)(struct graphic_context*, struct graphic_resource*,
	                struct graphic_transfer_cmd*);
	int (*transfer2)(struct graphic_context*, struct graphic_resource*,
	                 struct graphic_transfer2_cmd*);
	int (*transfer_from)(struct graphic_context*, struct graphic_resource*,
	                     struct graphic_transfer_cmd*);
};

struct graphic_context {
	uint32_t id;
	char* name;
	uint32_t nlen;
	uint32_t context_init;

	struct graphic_device* device;

	void* priv;

	struct graphic_context_ops* ops;

	struct graphic_context* next;
} __attribute__((aligned(64)));

struct graphic_device* graphic_get_device(uint16_t id);
struct graphic_device* create_graphic_device();
struct graphic_scanout* graphic_alloc_scanout(int id,
                                              struct graphic_device* device,
                                              int width, int height, int x,
                                              int y);

/* IOCTL */
// will be copied to lib vxair
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
	GRAPHIC_IOCTL_CREATE_RESOURCE3D,
};

struct graphic_ioctl_use_3d_cmd {
	uint32_t enable_3d;
} __attribute__((packed));

struct graphic_ioctl_create_resource_cmd {
	struct graphic_resource_desc desc;
	uint32_t resource_id;
};

struct graphic_ioctl_create_context_cmd {
	struct graphic_context_desc desc;
	uint32_t context_id;
};

struct graphic_ioctl_attach_backing_cmd {
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

struct graphic_ioctl_bind_resource_cmd {
	uint32_t context_id;
	uint32_t resource_id;
};

/* old api, will be deprecated */
void put_pixel(int x, int y, uint32_t color);
void put_pixel_alpha(int x, int y, pixel_t src);
void putc(char c, int x, int y, uint32_t fg, uint32_t bg);
void putc_nolock(char c, int col, int row, uint32_t fg, uint32_t bg);
void putc_utf8(const char* s, int col, int row, uint32_t fg, uint32_t bg);
void putc_utf8_nolock(const char* s, int col, int row, uint32_t fg,
                      uint32_t bg);
int utf8_char_len(uint8_t c);
void put_pixel_alpha_fast(int x, int y, pixel_t src);

uint32_t vxGetWidth(void);
uint32_t vxGetHeight(void);
void vxScroll(int px, uint32_t bg_color);
void vxScroll_nolock(int px, uint32_t bg_color);
void vxScrollDown(int px, uint32_t bg_color);
void vxScrollDown_nolock(int px, uint32_t bg_color);
void vxScrollRegion(int px, int top_row, int bottom_row, uint32_t bg_color);
void vxScrollRegion_nolock(int px, int top_row, int bottom_row,
                           uint32_t bg_color);
void vxScrollDownRegion(int px, int top_row, int bottom_row, uint32_t bg_color);
void vxScrollDownRegion_nolock(int px, int top_row, int bottom_row,
                               uint32_t bg_color);
void clear_screen(uint32_t color);
uint32_t screen_cols(void);
uint32_t screen_rows(void);

void fill_rect(int x, int y, int w, int h, uint32_t color);
void fill_rect_nolock(int x, int y, int w, int h, uint32_t color);

uint8_t* graphic_alloc_backbuffer(void);
void graphic_free_backbuffer(uint8_t* buffer);
void graphic_set_draw_buffer(uint8_t* buffer);
void graphic_flush_backbuffer(const uint8_t* backbuffer);
void graphic_flush_backbuffer_rows(const uint8_t* backbuffer, int start_row,
                                   int end_row);
void graphic_flush_backbuffer_rows_nolock(const uint8_t* backbuffer,
                                          int start_row, int end_row);

void graphic_lock(void);
void graphic_unlock(void);
bool graphic_is_locked(void);

void graphic_set_font_size(int size);

extern int g_font_size;

#ifdef __cplusplus
}
#endif

#endif // __HAL__GRAPHIC_H__