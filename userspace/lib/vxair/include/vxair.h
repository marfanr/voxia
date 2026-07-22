#ifndef VXAIR_H
#define VXAIR_H

#include <stddef.h>
#include <stdint.h>

struct vxair_device;
typedef struct vxair_device vxair_device_t;

struct vxair_context;
typedef struct vxair_context vxair_context_t;

struct vxair_buffer;
typedef struct vxair_buffer vxair_buffer_t;

struct vxair_shader;
typedef struct vxair_shader vxair_shader_t;

struct vxair_texture;
typedef struct vxair_texture vxair_texture_t;

typedef enum {
	VXAIR_BACKEND_VIRGL = 0,
	VXAIR_BACKEND_SOFTPIPE,
	VXAIR_BACKEND_LLVMPIPE
} vxair_backend_t;

typedef enum {
	VXAIR_SHADER_VERTEX = 0,
	VXAIR_SHADER_FRAGMENT
} vxair_shader_type_t;

typedef enum {
	VXAIR_PRIMITIVE_TRIANGLES = 4,
	VXAIR_PRIMITIVE_LINES = 1,
} vxair_primitive_t;

typedef enum {
    VXAIR_VERTEX_FORMAT_FLOAT1 = 0,
    VXAIR_VERTEX_FORMAT_FLOAT2,
    VXAIR_VERTEX_FORMAT_FLOAT3,
    VXAIR_VERTEX_FORMAT_FLOAT4
} vxair_vertex_format_t;

typedef struct {
    uint32_t offset;
    vxair_vertex_format_t format;
} vxair_vertex_element_t;

typedef enum {
    VXAIR_FORMAT_NONE = 0,
    VXAIR_FORMAT_R8G8B8A8_UNORM,
    VXAIR_FORMAT_B8G8R8A8_UNORM,
    VXAIR_FORMAT_B8G8R8X8_UNORM,
    VXAIR_FORMAT_A8R8G8B8_UNORM,
    VXAIR_FORMAT_X8R8G8B8_UNORM,
    VXAIR_FORMAT_B5G5R5A1_UNORM,
    VXAIR_FORMAT_B4G4R4A4_UNORM,
    VXAIR_FORMAT_B5G6R5_UNORM,
    VXAIR_FORMAT_R10G10B10A2_UNORM,
    VXAIR_FORMAT_L8_UNORM,
    VXAIR_FORMAT_A8_UNORM,
    VXAIR_FORMAT_I8_UNORM,
    VXAIR_FORMAT_L8A8_UNORM,
    VXAIR_FORMAT_L16_UNORM,
    VXAIR_FORMAT_UYVY,
    VXAIR_FORMAT_YUYV,
    VXAIR_FORMAT_Z16_UNORM,
    VXAIR_FORMAT_Z32_UNORM,
    VXAIR_FORMAT_Z32_FLOAT,
    VXAIR_FORMAT_Z24_UNORM_S8_UINT,
    VXAIR_FORMAT_S8_UINT_Z24_UNORM,
    VXAIR_FORMAT_Z24X8_UNORM,
    VXAIR_FORMAT_X8Z24_UNORM,
    VXAIR_FORMAT_S8_UINT,
    VXAIR_FORMAT_R64_FLOAT,
    VXAIR_FORMAT_R64G64_FLOAT,
    VXAIR_FORMAT_R64G64B64_FLOAT,
    VXAIR_FORMAT_R64G64B64A64_FLOAT,
    VXAIR_FORMAT_R32_FLOAT,
    VXAIR_FORMAT_R32G32_FLOAT,
    VXAIR_FORMAT_R32G32B32_FLOAT,
    VXAIR_FORMAT_R32G32B32A32_FLOAT,

    VXAIR_FORMAT_RGBA8 = VXAIR_FORMAT_R8G8B8A8_UNORM,
    VXAIR_FORMAT_BGRA8 = VXAIR_FORMAT_B8G8R8A8_UNORM,
    VXAIR_FORMAT_RGB565 = VXAIR_FORMAT_B5G6R5_UNORM,
} vxair_format_t;

// --- 1. Manajemen Perangkat & Konteks ---
vxair_device_t* vxair_device_create(vxair_backend_t backend);
void vxair_device_destroy(vxair_device_t* dev);
vxair_context_t* vxair_context_create(vxair_device_t* device, uint32_t width,
                                      uint32_t height);
void vxair_context_destroy(vxair_context_t* ctx);
uint32_t vxair_context_get_scanout_id(vxair_context_t* ctx);

// --- 2. Manajemen Resource ---
vxair_buffer_t* vxair_buffer_create(vxair_device_t* dev, size_t size,
                                    const void* data);
void vxair_buffer_destroy(vxair_device_t* dev, vxair_buffer_t* buf);
size_t vxair_buffer_get_size(vxair_buffer_t* buf);
void vxair_buffer_update(vxair_device_t* dev, vxair_buffer_t* buf,
                          size_t offset, size_t size, const void* data);
vxair_shader_t* vxair_shader_create(vxair_device_t* dev,
                                    vxair_shader_type_t type,
                                    const char* source);
vxair_texture_t* vxair_texture_create(vxair_device_t* dev, uint32_t width, uint32_t height, vxair_format_t format, const void* pixels);
vxair_texture_t* vxair_texture_import(vxair_device_t* dev, uint32_t resource_id, uint32_t width, uint32_t height, vxair_format_t format);
vxair_texture_t* vxair_cursor_create(vxair_device_t* dev, uint32_t width, uint32_t height, vxair_format_t format, const void* pixels);
void vxair_texture_update(vxair_device_t* dev, vxair_texture_t* tex, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* pixels);
uint32_t vxair_texture_get_resource_id(vxair_texture_t* tex);
// --- 3. Command Stream (Instruksi Render) ---
void vxair_cmd_clear(vxair_context_t* ctx, float r, float g, float b, float a);
void vxair_cmd_bind_shader(vxair_context_t* ctx, vxair_shader_t* shader);
void vxair_cmd_bind_vertex_buffer(vxair_context_t* ctx, vxair_buffer_t* vbo,
                                  uint32_t stride);
void vxair_cmd_set_viewport(vxair_context_t* ctx, float x, float y, float width,
                            float height, float min_z, float max_z);
void vxair_cmd_draw_arrays(vxair_context_t* ctx, vxair_primitive_t mode,
                           uint32_t start, uint32_t count);

void vxair_texture_attach(vxair_device_t* dev, vxair_texture_t* tex, vxair_context_t* ctx);
void vxair_cmd_bind_texture(vxair_context_t* ctx, uint32_t slot, vxair_texture_t* tex);
void vxair_cmd_set_constant_buffer(vxair_context_t* ctx, vxair_shader_type_t shader_type, uint32_t index, uint32_t size, const void* data);


typedef enum {
	VXAIR_FILTER_NEAREST = 0,
	VXAIR_FILTER_LINEAR = 1,
} vxair_filter_t;

typedef enum {
	VXAIR_MIP_FILTER_NONE = 0,
	VXAIR_MIP_FILTER_NEAREST,
	VXAIR_MIP_FILTER_LINEAR,
} vxair_mip_filter_t;

typedef enum {
	VXAIR_WRAP_REPEAT = 0,
	VXAIR_WRAP_CLAMP,
	VXAIR_WRAP_CLAMP_TO_EDGE,
	VXAIR_WRAP_CLAMP_TO_BORDER,
	VXAIR_WRAP_MIRROR_REPEAT,
	VXAIR_WRAP_MIRROR_CLAMP,
	VXAIR_WRAP_MIRROR_CLAMP_TO_EDGE,
	VXAIR_WRAP_MIRROR_CLAMP_TO_BORDER,
} vxair_wrap_t;

void vxair_cmd_bind_vertex_elements(vxair_context_t* ctx, uint32_t num_elements, const vxair_vertex_element_t* elements);
void vxair_cmd_set_sampler_filter(vxair_context_t* ctx, uint32_t slot, vxair_filter_t min_filter, vxair_filter_t mag_filter);
// --- 4. Eksekusi ---
void vxair_submit_and_present(
    vxair_context_t* ctx); // Kirim instruksi & tampilkan ke layar

void vxair_read_pixels(vxair_context_t* ctx, uint32_t x, uint32_t y, uint32_t width, uint32_t height, void* pixels);

void vxair_update_cursor(vxair_device_t* dev, uint32_t scanout_id, uint32_t resource_id, uint32_t hot_x, uint32_t hot_y);
void vxair_move_cursor(vxair_device_t* dev, uint32_t scanout_id, uint32_t x, uint32_t y);

void vxair_switch_context(vxair_device_t* dev, vxair_context_t * ctx);
void vxair_set_scanout(vxair_device_t* dev, vxair_context_t* ctx, int scanout_id);
void vxair_clear_cmd(vxair_device_t* dev, vxair_context_t* ctx);
uint32_t vxair_create_alpha_blend(vxair_context_t* ctx);
void vxair_bind_blend(vxair_context_t* ctx, uint32_t blend_id);

// --- 5. Scissor (Clipping) ---
void vxair_cmd_set_scissor(vxair_context_t* ctx, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height);
void vxair_cmd_disable_scissor(vxair_context_t* ctx);

#endif
