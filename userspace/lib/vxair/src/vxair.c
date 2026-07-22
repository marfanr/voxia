#include "vxair_internal.h"
#include <stdio.h>
#include <stdlib.h> // Force rebuild 2

extern int virgl_backend_init(struct vxair_device* dev);
extern int llvmpipe_backend_init(struct vxair_device* dev);

vxair_device_t* vxair_device_create(vxair_backend_t backend) {
	struct vxair_device* dev = calloc(1, sizeof(struct vxair_device));
	if (!dev)
		return NULL;

	dev->backend = backend;

	if (backend == VXAIR_BACKEND_VIRGL) {
		if (virgl_backend_init(dev) != 0) {
			free(dev);
			return NULL;
		}
	} else if (backend == VXAIR_BACKEND_LLVMPIPE) {
		if (llvmpipe_backend_init(dev) != 0) {
			free(dev);
			return NULL;
		}
	} else {
		free(dev);
		return NULL;
	}
	return dev;
}

vxair_context_t* vxair_context_create(vxair_device_t* device, uint32_t width,
                                      uint32_t height) {
	if (device && device->ops && device->ops->create_context) {
		vxair_context_t* ctx = device->ops->create_context(device, width, height);
		if (ctx) {
			device->ctx = ctx;
			device->active_context_id = ctx->context_id;
		}
		return ctx;
	}
	return NULL;
}

uint32_t vxair_context_get_scanout_id(vxair_context_t* ctx) {
	return ctx ? ctx->scanout_res_id : 0;
}

vxair_buffer_t* vxair_buffer_create(vxair_device_t* dev, size_t size,
                                    const void* data) {
	if (dev && dev->ops && dev->ops->create_buffer) {
		return dev->ops->create_buffer(dev, size, data);
	}
	return NULL;
}

void vxair_buffer_destroy(vxair_device_t* dev, vxair_buffer_t* buf) {
	if (!buf)
		return;
	if (dev && dev->ops && dev->ops->destroy_buffer) {
		dev->ops->destroy_buffer(dev, buf);
	}
	free(buf);
}

size_t vxair_buffer_get_size(vxair_buffer_t* buf) {
	return buf ? buf->size : 0;
}

void vxair_buffer_update(vxair_device_t* dev, vxair_buffer_t* buf,
                          size_t offset, size_t size, const void* data) {
	if (dev && dev->ops && dev->ops->update_buffer) {
		dev->ops->update_buffer(dev, buf, offset, size, data);
	}
}

vxair_shader_t* vxair_shader_create(vxair_device_t* dev,
                                    vxair_shader_type_t type,
                                    const char* source) {
	if (dev && dev->ops && dev->ops->create_shader) {
		return dev->ops->create_shader(dev, type, source);
	}
	return NULL;
}

vxair_texture_t* vxair_texture_create(vxair_device_t* dev, uint32_t width,
                                      uint32_t height, vxair_format_t format, const void* pixels) {
	if (dev && dev->ops && dev->ops->create_texture) {
		return dev->ops->create_texture(dev, width, height, format, pixels);
	}
	return NULL;
}

vxair_texture_t* vxair_texture_import(vxair_device_t* dev, uint32_t resource_id,
                                      uint32_t width, uint32_t height,
                                      vxair_format_t format) {
	if (dev && dev->ops && dev->ops->import_texture) {
		return dev->ops->import_texture(dev, resource_id, width, height, format);
	}
	return NULL;
}

vxair_texture_t* vxair_cursor_create(vxair_device_t* dev, uint32_t width, uint32_t height, vxair_format_t format, const void* pixels) {
	if (dev && dev->ops && dev->ops->create_cursor) {
		return dev->ops->create_cursor(dev, width, height, format, pixels);
	}
	return NULL;
}

void vxair_texture_update(vxair_device_t* dev, vxair_texture_t* tex, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* pixels) {
	if (dev && dev->ops && dev->ops->update_texture) {
		dev->ops->update_texture(dev, tex, x, y, width, height, pixels);
	}
}

uint32_t vxair_texture_get_resource_id(vxair_texture_t* tex) {
	if (tex) return tex->resource_id;
	return 0;
}

void vxair_cmd_set_viewport(vxair_context_t* ctx, float x, float y, float width,
                            float height, float min_z, float max_z) {
	if (ctx && ctx->ops && ctx->ops->cmd_set_viewport) {
		ctx->ops->cmd_set_viewport(ctx, x, y, width, height, min_z,
		                           max_z);
	}
}

void vxair_cmd_clear(vxair_context_t* ctx, float r, float g, float b, float a) {
	if (ctx && ctx->ops && ctx->ops->cmd_clear) {
		ctx->ops->cmd_clear(ctx, r, g, b, a);
	}
}

void vxair_cmd_bind_shader(vxair_context_t* ctx, vxair_shader_t* shader) {
	if (ctx && ctx->ops && ctx->ops->cmd_bind_shader) {
		ctx->ops->cmd_bind_shader(ctx, shader);
	}
}

void vxair_cmd_bind_vertex_buffer(vxair_context_t* ctx, vxair_buffer_t* vbo,
                                  uint32_t stride) {
	if (ctx && ctx->ops && ctx->ops->cmd_bind_vertex_buffer) {
		ctx->ops->cmd_bind_vertex_buffer(ctx, vbo, stride);
	}
}

void vxair_cmd_draw_arrays(vxair_context_t* ctx, vxair_primitive_t mode,
                           uint32_t start, uint32_t count) {
	if (ctx && ctx->ops && ctx->ops->cmd_draw_arrays) {
		ctx->ops->cmd_draw_arrays(ctx, mode, start, count);
	}
}

void vxair_submit_and_present(vxair_context_t* ctx) {
	if (ctx && ctx->ops && ctx->ops->submit_and_present) {
		ctx->ops->submit_and_present(ctx);
	}
}

void vxair_cmd_bind_texture(vxair_context_t* ctx, uint32_t slot,
                            vxair_texture_t* tex) {
	if (ctx && ctx->ops && ctx->ops->cmd_bind_texture) {
		ctx->ops->cmd_bind_texture(ctx, slot, tex);
	}
}

void vxair_cmd_set_constant_buffer(vxair_context_t* ctx,
                                   vxair_shader_type_t shader_type,
                                   uint32_t index, uint32_t size,
                                   const void* data) {
	if (ctx && ctx->ops && ctx->ops->cmd_set_constant_buffer) {
		ctx->ops->cmd_set_constant_buffer(ctx, shader_type, index, size,
		                                  data);
	}
}

void vxair_cmd_bind_vertex_elements(vxair_context_t* ctx, uint32_t num_elements, const vxair_vertex_element_t* elements) {
    if (ctx && ctx->ops && ctx->ops->cmd_bind_vertex_elements) {
        ctx->ops->cmd_bind_vertex_elements(ctx, num_elements, elements);
    }
}

void vxair_cmd_set_sampler_filter(vxair_context_t* ctx, uint32_t slot, vxair_filter_t min_filter, vxair_filter_t mag_filter) {
	if (ctx && ctx->ops && ctx->ops->cmd_set_sampler_filter) {
		ctx->ops->cmd_set_sampler_filter(ctx, slot, min_filter, mag_filter);
	}
}

void vxair_read_pixels(vxair_context_t* ctx, uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height, void* pixels) {
	if (ctx && ctx->ops && ctx->ops->read_pixels) {
		ctx->ops->read_pixels(ctx, x, y, width, height, pixels);
	}
}

void vxair_update_cursor(vxair_device_t* dev, uint32_t scanout_id, uint32_t resource_id, uint32_t hot_x, uint32_t hot_y) {
	if (dev && dev->ops && dev->ops->update_cursor) {
		dev->ops->update_cursor(dev, scanout_id, resource_id, hot_x, hot_y);
	}
}

void vxair_move_cursor(vxair_device_t* dev, uint32_t scanout_id, uint32_t x, uint32_t y) {
	if (dev && dev->ops && dev->ops->move_cursor) {
		dev->ops->move_cursor(dev, scanout_id, x, y);
	}
}

void vxair_switch_context(vxair_device_t* dev, vxair_context_t * ctx) {
	if (!dev || !ctx) return;
	dev->active_context_id = ctx->context_id;
	dev->ctx = ctx;
}

void vxair_texture_attach(vxair_device_t* dev, vxair_texture_t* tex, vxair_context_t* ctx) {
	dev->ops->attach_texture(dev, tex, ctx);
}

void vxair_set_scanout(vxair_device_t* dev, vxair_context_t* ctx, int scanout_id) {
	dev->ops->set_scanout(dev, ctx, scanout_id);
}

void vxair_clear_cmd(vxair_device_t* dev, vxair_context_t* ctx) {
	dev->ops->clear_cmd(ctx);
}

uint32_t vxair_create_alpha_blend(vxair_context_t* ctx) {
	return ctx->ops->create_alpha_blend(ctx);
}

void vxair_bind_blend(vxair_context_t* ctx, uint32_t blend_id) {
	ctx->ops->bind_blend(ctx, blend_id);
}

void vxair_cmd_set_scissor(vxair_context_t* ctx, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height) {
	if (ctx && ctx->ops && ctx->ops->cmd_set_scissor) {
		ctx->ops->cmd_set_scissor(ctx, x, y, width, height);
	}
}

void vxair_cmd_disable_scissor(vxair_context_t* ctx) {
	if (ctx && ctx->ops && ctx->ops->cmd_disable_scissor) {
		ctx->ops->cmd_disable_scissor(ctx);
	}
}