#include "../virgl/graphic_ioctl.h"
#include "vxair_internal.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static uint32_t next_res_id = 3000;

static void llvmpipe_cmd_set_viewport(struct vxair_context* ctx, float x, float y, float w, float h, float min_z, float max_z) {}
static void llvmpipe_cmd_bind_shader(struct vxair_context* ctx, struct vxair_shader* shader) {}
static void llvmpipe_cmd_bind_vertex_buffer(struct vxair_context* ctx, struct vxair_buffer* vbo, uint32_t stride) {
    if (ctx) ctx->bound_vbo = vbo;
}
static void llvmpipe_cmd_draw_arrays(struct vxair_context* ctx, vxair_primitive_t mode, uint32_t start, uint32_t count) {
    if (!ctx || !ctx->cpu_buffer || !ctx->bound_vbo || !ctx->bound_vbo->cpu_data) return;

    float* v_data = (float*)ctx->bound_vbo->cpu_data;
    float min_x = 99999, min_y = 99999, max_x = -99999, max_y = -99999;
    
    for(uint32_t i=0; i<count; i++) {
        float vx = v_data[i * 4 + 0];
        float vy = v_data[i * 4 + 1];
        if (vx < min_x) min_x = vx;
        if (vy < min_y) min_y = vy;
        if (vx > max_x) max_x = vx;
        if (vy > max_y) max_y = vy;
    }
    
    int dst_x = (int)min_x;
    int dst_y = (int)min_y;
    int dst_w = (int)(max_x - min_x);
    int dst_h = (int)(max_y - min_y);
    
    if (dst_x < 0) dst_x = 0;
    if (dst_y < 0) dst_y = 0;
    if (dst_x + dst_w > 1280) dst_w = 1280 - dst_x;
    if (dst_y + dst_h > 720) dst_h = 720 - dst_y;
    
    if (dst_w <= 0 || dst_h <= 0) return;

    uint32_t* fb = (uint32_t*)ctx->cpu_buffer;
    
    if (ctx->bound_tex && ctx->bound_tex->cpu_pixels) {
        uint32_t* tex_pixels = (uint32_t*)ctx->bound_tex->cpu_pixels;
        uint32_t tex_w = ctx->bound_tex->width;
        uint32_t tex_h = ctx->bound_tex->height;
        
        for (int y = 0; y < dst_h; y++) {
            int src_y = (y * tex_h) / dst_h;
            if (src_y >= (int)tex_h) src_y = tex_h - 1;
            
            for (int x = 0; x < dst_w; x++) {
                int src_x = (x * tex_w) / dst_w;
                if (src_x >= (int)tex_w) src_x = tex_w - 1;
                
                uint32_t p = tex_pixels[src_y * tex_w + src_x];
                uint32_t a = (p >> 24) & 0xFF;
                if (a > 0) {
                    fb[(dst_y + y) * 1280 + (dst_x + x)] = p;
                }
            }
        }
    }
}
static void llvmpipe_cmd_bind_texture(struct vxair_context* ctx, uint32_t slot, struct vxair_texture* tex) {
    if (ctx && slot == 0) ctx->bound_tex = tex;
}
static void llvmpipe_cmd_set_constant_buffer(struct vxair_context* ctx, vxair_shader_type_t shader_type, uint32_t index, uint32_t size, const void* data) {}
static void llvmpipe_cmd_bind_vertex_elements(struct vxair_context* ctx, uint32_t num_elements, const vxair_vertex_element_t* elements) {}
static void llvmpipe_cmd_set_sampler_filter(struct vxair_context* ctx, uint32_t slot, vxair_filter_t min_filter, vxair_filter_t mag_filter) {}

static void llvmpipe_cmd_clear(struct vxair_context* ctx, float r, float g, float b, float a) {
    if (!ctx || !ctx->cpu_buffer) return;
    
    // Menggambar langsung ke CPU buffer bawaan scanout context
    uint32_t color = ((uint32_t)(a * 255.0f) << 24) |
                     ((uint32_t)(r * 255.0f) << 16) | // Asumsi B8G8R8A8 / R8G8B8A8 terbalik
                     ((uint32_t)(g * 255.0f) << 8) |
                     ((uint32_t)(b * 255.0f));
    
    uint32_t* pixels = (uint32_t*)ctx->cpu_buffer;
    for(int i = 0; i < 1280 * 720; i++) {
        pixels[i] = color;
    }
}

static void llvmpipe_submit_and_present(struct vxair_context* ctx) {
    if (!ctx || !ctx->scanout_res_id || !ctx->cpu_buffer) return;
    
    // Upload cpu_buffer ke resource QEMU via transfer 2D
    struct graphic_transfer_cmd transfer = {
        .context_id = ctx->kctx_id,
        .resource_id = ctx->scanout_res_id,
        .offset = 0,
        .level = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = 1280,
        .h = 720,
        .d = 1,
        .data = ctx->cpu_buffer,
    };
    ioctl(ctx->dev->card_fd, GRAPHIC_IOCTL_TRANSFER, &transfer);

    // Flush resource ke layar (host)
    struct graphic_ioctl_resource_flush_cmd flush = {
        .resource_id = ctx->scanout_res_id,
        .x = 0,
        .y = 0,
        .width = 1280,
        .height = 720
    };
    ioctl(ctx->dev->card_fd, GRAPHIC_IOCTL_RESOURCE_FLUSH, &flush);
}

static void llvmpipe_ctx_destroy(struct vxair_context* ctx) {
    if (ctx->cpu_buffer) free(ctx->cpu_buffer);
    free(ctx);
}

static void llvmpipe_read_pixels(struct vxair_context* ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, void* pixels) {
    if (!ctx || !ctx->cpu_buffer || !pixels) return;
    
    uint32_t* src = (uint32_t*)ctx->cpu_buffer;
    uint32_t* dst = (uint32_t*)pixels;
    
    for(uint32_t row = 0; row < h; row++) {
        for(uint32_t col = 0; col < w; col++) {
            dst[row * w + col] = src[(y + row) * 1280 + (x + col)];
        }
    }
}

static struct vxair_context_ops llvmpipe_ctx_ops = {
    .destroy = llvmpipe_ctx_destroy,
    .cmd_set_viewport = llvmpipe_cmd_set_viewport,
    .cmd_clear = llvmpipe_cmd_clear,
    .cmd_bind_shader = llvmpipe_cmd_bind_shader,
    .cmd_bind_vertex_buffer = llvmpipe_cmd_bind_vertex_buffer,
    .cmd_draw_arrays = llvmpipe_cmd_draw_arrays,
    .cmd_bind_texture = llvmpipe_cmd_bind_texture,
    .cmd_set_constant_buffer = llvmpipe_cmd_set_constant_buffer,
    .cmd_bind_vertex_elements = llvmpipe_cmd_bind_vertex_elements,
    .submit_and_present = llvmpipe_submit_and_present,
    .read_pixels = llvmpipe_read_pixels,
    .cmd_set_sampler_filter = llvmpipe_cmd_set_sampler_filter,
};

static struct vxair_context* llvmpipe_create_context(struct vxair_device* dev, uint32_t w, uint32_t h) {
    struct vxair_context* ctx = calloc(1, sizeof(*ctx));
    ctx->dev = dev;
    ctx->ops = &llvmpipe_ctx_ops;
    
    // Create Kernel Context
    uint32_t kctx_id = 11;
    struct graphic_ioctl_create_context_cmd ctx_cmd = {
        .desc = {.id = kctx_id, .name = "llvmp", .nlen = 5, .context_init = 1},
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_CREATE_CONTEXT, &ctx_cmd);
    ctx->kctx_id = kctx_id;
    
    uint32_t res_id = __atomic_fetch_add(&next_res_id, 1, __ATOMIC_SEQ_CST);
    
    // Resource scanout 2D
    struct graphic_ioctl_create_resource_cmd res_cmd = {
        .desc = {
            .id = res_id,
            .type = 2, // 2D Texture
            .format = 1, // R8G8B8A8
            .bind = (1 << 6), // GRAPHIC_BIND_2D
            .width = w,
            .height = h,
            .depth = 1,
            .array_size = 1,
            .mip_levels = 1,
            .sample_counts = 1,
            .ctx_id = kctx_id,
        },
        .resource_id = 0,
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_CREATE_RESOURCE, &res_cmd);
    
    ctx->scanout_res_id = res_cmd.resource_id;
    ctx->cpu_buffer = malloc(w * h * 4);
    memset(ctx->cpu_buffer, 0, w * h * 4);
    
    // Attach backing
    struct graphic_ioctl_attach_backing_cmd att_cmd = {
        .resource_id = res_cmd.resource_id,
        .vaddr = (uintptr_t)ctx->cpu_buffer,
        .size = w * h * 4
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_ATTACH_BACKING, &att_cmd);
    
    // Set scanout
    struct graphic_ioctl_set_scanout_cmd scanout_cmd = {
        .scanout_id = 0,
        .resource_id = res_cmd.resource_id
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_SET_SCANOUT, &scanout_cmd);
    
    return ctx;
}

static void llvmpipe_dev_destroy(struct vxair_device* dev) {
    close(dev->card_fd);
    free(dev);
}

static struct vxair_buffer* llvmpipe_create_buffer(struct vxair_device* dev, size_t size, const void* data) {
    struct vxair_buffer* buf = calloc(1, sizeof(*buf));
    buf->size = size;
    buf->cpu_data = malloc(size);
    if (data) memcpy(buf->cpu_data, data, size);
    return buf;
}

static void llvmpipe_destroy_buffer(struct vxair_device* dev, struct vxair_buffer* buf) {
    (void)dev;
    if (!buf)
        return;
    if (buf->cpu_data)
        free(buf->cpu_data);
}

static struct vxair_shader* llvmpipe_create_shader(struct vxair_device* dev, vxair_shader_type_t type, const char* src) {
    return NULL;
}

static struct vxair_texture* llvmpipe_create_texture(struct vxair_device* dev, uint32_t w, uint32_t h, vxair_format_t format, const void* pixels) {
    uint32_t res_id = __atomic_fetch_add(&next_res_id, 1, __ATOMIC_SEQ_CST);
    
    // Texture 2D biasa untuk aset
    struct graphic_ioctl_create_resource_cmd tex_cmd = {
        .desc = {
            .id = res_id,
            .type = 2,
            .format = format,
            .bind = (1 << 6), // GRAPHIC_BIND_2D
            .width = w,
            .height = h,
            .depth = 1,
            .array_size = 1,
            .mip_levels = 1,
            .sample_counts = 1,
            .ctx_id = 11, // Global llvmpipe context
        }
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_CREATE_RESOURCE, &tex_cmd);
    
    struct vxair_texture* tex = calloc(1, sizeof(*tex));
    tex->resource_id = tex_cmd.resource_id;
    tex->width = w;
    tex->height = h;
    tex->format = format;
    tex->cpu_pixels = malloc(w * h * 4);
    if (pixels) {
        memcpy(tex->cpu_pixels, pixels, w * h * 4);
    } else {
        memset(tex->cpu_pixels, 0, w * h * 4);
    }

    struct graphic_ioctl_attach_backing_cmd att_cmd = {
        .resource_id = tex_cmd.resource_id,
        .vaddr = (uintptr_t)tex->cpu_pixels,
        .size = w * h * 4
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_ATTACH_BACKING, &att_cmd);

    if (pixels) {
        struct graphic_transfer_cmd transfer = {
            .context_id = 11,
            .resource_id = tex_cmd.resource_id,
            .offset = 0,
            .level = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = w,
            .h = h,
            .d = 1,
            .data = (void*)pixels,
        };
        ioctl(dev->card_fd, GRAPHIC_IOCTL_TRANSFER, &transfer);
        
        struct graphic_ioctl_resource_flush_cmd flush = {
            .resource_id = tex_cmd.resource_id,
            .x = 0,
            .y = 0,
            .width = w,
            .height = h
        };
        ioctl(dev->card_fd, GRAPHIC_IOCTL_RESOURCE_FLUSH, &flush);
    }

    return tex;
}

static struct vxair_texture* llvmpipe_create_cursor(struct vxair_device* dev, uint32_t w, uint32_t h, vxair_format_t format, const void* pixels) {
    return llvmpipe_create_texture(dev, w, h, format, pixels);
}

static struct vxair_texture* llvmpipe_import_texture(struct vxair_device* dev, uint32_t resource_id, uint32_t w, uint32_t h, vxair_format_t format) {
    struct vxair_texture* tex = calloc(1, sizeof(*tex));
    tex->resource_id = resource_id;
    tex->width = w;
    tex->height = h;
    tex->format = format;
    return tex;
}

static void llvmpipe_texture_update(struct vxair_device* dev, struct vxair_texture* tex, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void* pixels) {
    if (!dev || !tex || !pixels) return;
    
    if (tex->cpu_pixels) {
        uint32_t* dst = (uint32_t*)tex->cpu_pixels;
        const uint32_t* src = (const uint32_t*)pixels;
        for (uint32_t row = 0; row < h; row++) {
            memcpy(&dst[(y + row) * tex->width + x], &src[row * w], w * 4);
        }
    }
    
    struct graphic_transfer_cmd transfer = {
        .context_id = 11,
        .resource_id = tex->resource_id,
        .offset = 0,
        .level = 0,
        .x = x,
        .y = y,
        .z = 0,
        .w = w,
        .h = h,
        .d = 1,
        .data = (void*)pixels,
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_TRANSFER, &transfer);
    
    struct graphic_ioctl_resource_flush_cmd flush = {
        .resource_id = tex->resource_id,
        .x = x,
        .y = y,
        .width = w,
        .height = h
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_RESOURCE_FLUSH, &flush);
}

static void llvmpipe_update_cursor(struct vxair_device* dev, uint32_t scanout_id, uint32_t resource_id, uint32_t hot_x, uint32_t hot_y) {
    if (!dev) return;
    struct graphic_ioctl_update_cursor_cmd cmd = {
        .scanout_id = scanout_id,
        .resource_id = resource_id,
        .hot_x = hot_x,
        .hot_y = hot_y
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_UPDATE_CURSOR, &cmd);
}

static void llvmpipe_move_cursor(struct vxair_device* dev, uint32_t scanout_id, uint32_t x, uint32_t y) {
    if (!dev) return;
    struct graphic_ioctl_move_cursor_cmd cmd = {
        .scanout_id = scanout_id, .x = x, .y = y
    };
    ioctl(dev->card_fd, GRAPHIC_IOCTL_MOVE_CURSOR, &cmd);
}

static struct vxair_device_ops llvmpipe_ops = {
    .destroy = llvmpipe_dev_destroy,
    .create_context = llvmpipe_create_context,
    .create_buffer = llvmpipe_create_buffer,
    .destroy_buffer = llvmpipe_destroy_buffer,
    .create_shader = llvmpipe_create_shader,
    .create_texture = llvmpipe_create_texture,
    .import_texture = llvmpipe_import_texture,
    .update_texture = llvmpipe_texture_update,
    .create_cursor = llvmpipe_create_cursor,
    .update_cursor = llvmpipe_update_cursor,
    .move_cursor = llvmpipe_move_cursor,
};

int llvmpipe_backend_init(struct vxair_device* dev) {
    dev->card_fd = open("/dev/dri/card0", O_RDWR);
    if (dev->card_fd < 0) return -1;
    dev->ops = &llvmpipe_ops;
    return 0;
}
