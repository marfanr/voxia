#include "graphic_ioctl.h"
#include "virgl_hw.h"
#include "vxair_internal.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "virgl_backend.h"

struct vxair_context_ops virgl_ctx_ops = {.destroy = virgl_ctx_destroy,
                                          .cmd_set_viewport = virgl_cmd_set_viewport,
                                          .cmd_clear = virgl_cmd_clear,
                                          .cmd_bind_shader = virgl_cmd_bind_shader,
                                          .cmd_bind_vertex_buffer = virgl_cmd_bind_vertex_buffer,
                                          .cmd_draw_arrays = virgl_cmd_draw_arrays,
                                          .cmd_bind_texture = virgl_cmd_bind_texture,
                                          .cmd_set_constant_buffer = virgl_cmd_set_constant_buffer,
                                          .cmd_bind_vertex_elements = virgl_cmd_bind_vertex_elements,
                                          .submit_and_present = virgl_submit_and_present,
                                          .read_pixels = virgl_read_pixels,
                                          .cmd_set_sampler_filter = virgl_cmd_set_sampler_filter,
                                          .create_alpha_blend = virgl_create_alpha_blend,
                                          .bind_blend = virgl_bind_blend,
                                          .cmd_set_scissor = virgl_cmd_set_scissor,
                                          .cmd_disable_scissor = virgl_cmd_disable_scissor};

static struct vxair_context* virgl_create_context(struct vxair_device* dev, uint32_t w, uint32_t h) {

	uint32_t client_ctx_id = __atomic_fetch_add(&next_ctx_id, 1, __ATOMIC_SEQ_CST);
	struct graphic_ioctl_create_context_cmd ctx_cmd = {
	    .desc = {.id = client_ctx_id, .context_init = 2, .name = "vxair", .nlen = 5},
	    .context_id = 0,
	};

	int ret = ioctl(dev->card_fd, GRAPHIC_IOCTL_CREATE_CONTEXT, &ctx_cmd);
	if (ret != 0)
		return NULL;

	struct vxair_context* ctx = calloc(1, sizeof(struct vxair_context));
	ctx->dev = dev;
	ctx->context_id = ctx_cmd.context_id;
	ctx->ops = &virgl_ctx_ops;
	ctx->next_res_id = 30;
	ctx->virgl_next_obj_id = 1000;

	uint32_t scanout_res = __atomic_fetch_add(&ctx->next_res_id, 1, __ATOMIC_SEQ_CST);
	struct graphic_ioctl_create_resource_cmd res_cmd = {
	    .desc =
	        {
	            .id = scanout_res,
	            .type = 2,                       // GRAPHIC_RESOURCE_TEXTURE_2D
	            .format = 1,                     // GRAPHIC_FORMAT_R8G8B8A8_UNORM = 1
	            .bind = 1 | (1 << 4) | (1 << 5), // GRAPHIC_BIND_RENDER_TARGET |
	                                             // GRAPHIC_BIND_TEXTURE |
	                                             // GRAPHIC_BIND_SCANOUT
	            .width = w,
	            .height = h,
	            .depth = 1,
	            .array_size = 1,
	            .mip_levels = 1,
	            .sample_counts = 1,
	            .ctx_id = ctx->context_id,
	        },
	    .resource_id = 0,
	};
	if (ioctl(dev->card_fd, GRAPHIC_IOCTL_CREATE_RESOURCE, &res_cmd) != 0)
		return NULL;

	struct graphic_ioctl_bind_resource_cmd bind_cmd = {
	    .context_id = ctx_cmd.context_id,
	    .resource_id = res_cmd.resource_id,
	};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_BIND_RESOURCE, &bind_cmd);

	struct graphic_ioctl_attach_backing_cmd att_cmd = {.resource_id = res_cmd.resource_id};
	if (ioctl(dev->card_fd, GRAPHIC_IOCTL_ATTACH_BACKING, &att_cmd) != 0)
		return NULL;

	struct vxair_buffer* rt_buf = calloc(1, sizeof(struct vxair_buffer));
	rt_buf->resource_id = res_cmd.resource_id;
	rt_buf->size = (size_t)w * h * 4; // R8G8B8A8, buat referensi ukuran
	ctx->render_target = rt_buf;
	ctx->scanout_res_id = res_cmd.resource_id;

	// Create and bind default Surface for the scanout resource (virgl
	// internal object)
	uint32_t surf_id = __atomic_fetch_add(&ctx->virgl_next_obj_id, 1, __ATOMIC_SEQ_CST);
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, 5));
	virgl_emit_cmd(ctx, surf_id);
	virgl_emit_cmd(ctx, res_cmd.resource_id);
	virgl_emit_cmd(ctx, 1); // GRAPHIC_FORMAT_R8G8B8A8_UNORM = 1
	virgl_emit_cmd(ctx, 0); // val0
	virgl_emit_cmd(ctx, 0); // val1

	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE, 0, 3));
	virgl_emit_cmd(ctx, 1);       // nr_cbufs
	virgl_emit_cmd(ctx, 0);       // zsurf
	virgl_emit_cmd(ctx, surf_id); // cbufs[0]

	// Create and bind default Blend state
	// uint32_t blend_id = next_res_id++;
	// virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, 1,
	//                                11)); // VIRGL_OBJECT_BLEND
	// virgl_emit_cmd(ctx, blend_id);
	// virgl_emit_cmd(ctx, 0);
	// virgl_emit_cmd(ctx, 0);
	// virgl_emit_cmd(ctx, (0xF << 27)); // colormask for RT0
	// for (int i = 0; i < 7; i++)
	// 	virgl_emit_cmd(ctx, 0);
	// virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, 1, 1));
	// 	virgl_emit_cmd(ctx, blend_id);

	uint32_t blend_id = __atomic_fetch_add(&ctx->virgl_next_obj_id, 1, __ATOMIC_SEQ_CST);
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, 1,
	                               11)); // VIRGL_OBJECT_BLEND
	virgl_emit_cmd(ctx, blend_id);
	virgl_emit_cmd(ctx, 0); // independent_blend_enable=0, logicop_enable=0
	virgl_emit_cmd(ctx, 0); // logicop_func

	/* RT0: blend_enable=1, ADD, SRC_ALPHA / INV_SRC_ALPHA (rgb & alpha) */
	virgl_emit_cmd(ctx, (1 << 0) | (0 << 1) | (0x3 << 4) | (0x13 << 9) | (0 << 14) | (0x3 << 17) | (0x13 << 22) | (0xF << 27)); // 0x7CC62631

	for (int i = 0; i < 7; i++)
		virgl_emit_cmd(ctx, 0); // RT1..RT7 unused

	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, 1, 1));
	virgl_emit_cmd(ctx, blend_id);

	// Create and bind default Rasterizer state (virgl internal object)
	uint32_t rast_id = __atomic_fetch_add(&ctx->virgl_next_obj_id, 1, __ATOMIC_SEQ_CST);
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, 2,
	                               9)); // VIRGL_OBJECT_RASTERIZER
	virgl_emit_cmd(ctx, rast_id);
	virgl_emit_cmd(ctx, VIRGL_OBJ_RS_S0_DEPTH_CLIP(1) | VIRGL_OBJ_RS_S0_SCISSOR(1)); // S0: depth_clip=1, scissor=1
	for (int i = 0; i < 7; i++)
		virgl_emit_cmd(ctx, 0);

	// Create and bind default Sampler state (virgl internal object)
	uint32_t sampler_id = __atomic_fetch_add(&ctx->virgl_next_obj_id, 1, __ATOMIC_SEQ_CST);
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, 7,
	                               9)); // 7 = VIRGL_OBJECT_SAMPLER_STATE
	virgl_emit_cmd(ctx, sampler_id);

	// min_img(9-10), min_mip(11-12), mag_img(13-14) Nearest=0, Linear=1.
	// We use Linear (1) for smoother scaling
	virgl_emit_cmd(ctx, (1 << 9) | (1 << 13));
	for (int i = 0; i < 7; i++)
		virgl_emit_cmd(ctx, 0);

	// Bind Sampler state to slot 0 for FRAGMENT shader
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_BIND_SAMPLER_STATES, 0, 3)); // len=3
	virgl_emit_cmd(ctx, 1);                                                // shader_type=1 (FS)
	virgl_emit_cmd(ctx, 0);                                                // start_slot
	virgl_emit_cmd(ctx, sampler_id);

	// Bind states (which were originally Rasterizer/DSA zeros, wait, no,
	// they were BIND_OBJECT BLEND?) No, wait, earlier there was NO
	// BIND_OBJECT BLEND here! It was already bound at line 256! I
	// accidentally added BIND_OBJECT BLEND here because it was in the
	// TargetContent from my replacement. Let me just remove it completely.
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, 2,
	                               1)); // 2 = VIRGL_OBJECT_RASTERIZER
	virgl_emit_cmd(ctx, rast_id);

	/* Set full-screen scissor rect so scissor=1 doesn't clip everything */
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_SCISSOR_STATE, 0, 3));
	virgl_emit_cmd(ctx, 0);                       // start_slot
	virgl_emit_cmd(ctx, 0 | (0 << 16));           // minx=0, miny=0
	virgl_emit_cmd(ctx, 0xFFFF | (0xFFFF << 16)); // maxx=65535, maxy=65535

	// Create and bind default DSA state (virgl internal object)
	uint32_t dsa_id = __atomic_fetch_add(&ctx->virgl_next_obj_id, 1, __ATOMIC_SEQ_CST);
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, 3,
	                               5)); // VIRGL_OBJECT_DSA
	virgl_emit_cmd(ctx, dsa_id);
	for (int i = 0; i < 4; i++)
		virgl_emit_cmd(ctx, 0);
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, 3, 1));
	virgl_emit_cmd(ctx, dsa_id);

	// Create and bind default Vertex Elements state (virgl internal object)
	uint32_t ve_id = __atomic_fetch_add(&ctx->virgl_next_obj_id, 1, __ATOMIC_SEQ_CST);
	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, 5,
	                               9)); // VIRGL_OBJECT_VERTEX_ELEMENTS (2
	                                    // elements * 4 = 8 + 1 = 9)
	virgl_emit_cmd(ctx, ve_id);
	// Element 0: Position
	virgl_emit_cmd(ctx, 0); // src_offset = 0
	virgl_emit_cmd(ctx, 0); // instance_divisor = 0
	virgl_emit_cmd(ctx, 0); // vertex_buffer_index = 0
	virgl_emit_cmd(ctx,
	               31); // PIPE_FORMAT_R32G32B32A32_FLOAT = 30 or 31 (test used 31)
	// Element 1: Color
	virgl_emit_cmd(ctx, 16); // src_offset = 16 bytes
	virgl_emit_cmd(ctx, 0);  // instance_divisor = 0
	virgl_emit_cmd(ctx, 0);  // vertex_buffer_index = 0
	virgl_emit_cmd(ctx, 31); // PIPE_FORMAT... (test used 31)

	virgl_emit_cmd(ctx, VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, 5, 1));
	virgl_emit_cmd(ctx, ve_id);

	return ctx;
}

static void virgl_dev_destroy(struct vxair_device* dev) { free(dev); }

static struct vxair_buffer* virgl_create_buffer(struct vxair_device* dev, size_t size, const void* data) {
	uint32_t client_res_id = __atomic_fetch_add(&dev->ctx->next_res_id, 1, __ATOMIC_SEQ_CST);

	struct graphic_ioctl_create_resource_cmd vb_cmd = {
	    .desc =
	        {
	            .id = client_res_id,
	            .type = 0, // GRAPHIC_RESOURCE_BUFFER
	            .format = GRAPHIC_FORMAT_NONE,
	            .bind = 1 << 2, // GRAPHIC_BIND_VERTEX
	            .width = size,
	            .height = 1,
	            .depth = 1,
	            .array_size = 1,
	            .mip_levels = 0,
	            .sample_counts = 0,
	            .ctx_id = dev->active_context_id,
	        },
	    .resource_id = 0,
	};
	int ret = ioctl(dev->card_fd, GRAPHIC_IOCTL_CREATE_RESOURCE, &vb_cmd);
	if (ret != 0) {
		return NULL;
	}

	struct graphic_ioctl_bind_resource_cmd bind_cmd = {
	    .context_id = dev->active_context_id,
	    .resource_id = vb_cmd.resource_id,
	};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_BIND_RESOURCE, &bind_cmd);

	struct graphic_ioctl_attach_backing_cmd vb_att = {
	    .resource_id = vb_cmd.resource_id,
	};
	if (ioctl(dev->card_fd, GRAPHIC_IOCTL_ATTACH_BACKING, &vb_att) != 0)
		return NULL;

	if (data) {
		struct graphic_transfer_cmd vb_transfer = {
		    .context_id = dev->active_context_id,
		    .resource_id = vb_cmd.resource_id,
		    .offset = 0,
		    .level = 0,
		    .x = 0,
		    .y = 0,
		    .z = 0,
		    .w = size,
		    .h = 1,
		    .d = 1,
		    .data = (void*)data,
		};
		ioctl(dev->card_fd, GRAPHIC_IOCTL_TRANSFER, &vb_transfer);
	}

	struct vxair_buffer* buf = calloc(1, sizeof(struct vxair_buffer));
	buf->resource_id = vb_cmd.resource_id;
	buf->size = size;
	buf->cpu_data = NULL; // No persistent CPU mapping
	return buf;
}

static void virgl_destroy_buffer(struct vxair_device* dev, struct vxair_buffer* buf) {
	if (!dev || !buf)
		return;

	struct {
		uint32_t resource_id;
	} destroy_cmd = {.resource_id = buf->resource_id};

	ioctl(dev->card_fd, GRAPHIC_IOCTL_DESTROY_RESOURCE, &destroy_cmd);
}

static void virgl_buffer_update(struct vxair_device* dev, struct vxair_buffer* buf, size_t offset, size_t size, const void* data) {
	if (!dev || !buf || !data || size == 0)
		return;
	if (offset + size > buf->size)
		return;

	struct graphic_transfer_cmd transfer = {
	    .context_id = dev->active_context_id,
	    .resource_id = buf->resource_id,
	    .offset = offset,
	    .level = 0,
	    .x = 0,
	    .y = 0,
	    .z = 0,
	    .w = size,
	    .h = 1,
	    .d = 1,
	    .data = (void*)data,
	};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_TRANSFER, &transfer);
}

static struct vxair_shader* virgl_create_shader(struct vxair_device* dev, vxair_shader_type_t type, const char* src) {
	(void)dev;
	struct vxair_shader* shader = calloc(1, sizeof(struct vxair_shader));
	shader->type = type;
	/* Shader handles are virglrenderer INTERNAL objects, not kernel GPU
	 * resources. Use a separate ID space to avoid conflicts with kernel
	 * resource IDs. */
	static uint32_t shader_handle_id = 2000;
	shader->handle_id = __atomic_fetch_add(&shader_handle_id, 1, __ATOMIC_SEQ_CST);

	int len = 0;
	while (src[len])
		len++;
	shader->src = calloc(1, len + 1);
	for (int i = 0; i < len; i++)
		shader->src[i] = src[i];

	shader->created_in_ctx = 0;
	return shader;
}

static uint32_t vxair_to_virgl_format(vxair_format_t format) {
	switch (format) {
	case VXAIR_FORMAT_R8G8B8A8_UNORM:
		return VIRGL_FORMAT_R8G8B8A8_UNORM;
	case VXAIR_FORMAT_B8G8R8A8_UNORM:
		return VIRGL_FORMAT_B8G8R8A8_UNORM;
	case VXAIR_FORMAT_B8G8R8X8_UNORM:
		return VIRGL_FORMAT_B8G8R8X8_UNORM;
	case VXAIR_FORMAT_A8R8G8B8_UNORM:
		return VIRGL_FORMAT_A8R8G8B8_UNORM;
	case VXAIR_FORMAT_X8R8G8B8_UNORM:
		return VIRGL_FORMAT_X8R8G8B8_UNORM;
	case VXAIR_FORMAT_B5G5R5A1_UNORM:
		return VIRGL_FORMAT_B5G5R5A1_UNORM;
	case VXAIR_FORMAT_B4G4R4A4_UNORM:
		return VIRGL_FORMAT_B4G4R4A4_UNORM;
	case VXAIR_FORMAT_B5G6R5_UNORM:
		return VIRGL_FORMAT_B5G6R5_UNORM;
	case VXAIR_FORMAT_R10G10B10A2_UNORM:
		return VIRGL_FORMAT_R10G10B10A2_UNORM;
	case VXAIR_FORMAT_L8_UNORM:
		return VIRGL_FORMAT_L8_UNORM;
	case VXAIR_FORMAT_A8_UNORM:
		return VIRGL_FORMAT_A8_UNORM;
	case VXAIR_FORMAT_I8_UNORM:
		return VIRGL_FORMAT_I8_UNORM;
	case VXAIR_FORMAT_L8A8_UNORM:
		return VIRGL_FORMAT_L8A8_UNORM;
	case VXAIR_FORMAT_L16_UNORM:
		return VIRGL_FORMAT_L16_UNORM;
	case VXAIR_FORMAT_UYVY:
		return VIRGL_FORMAT_UYVY;
	case VXAIR_FORMAT_YUYV:
		return VIRGL_FORMAT_YUYV;
	case VXAIR_FORMAT_Z16_UNORM:
		return VIRGL_FORMAT_Z16_UNORM;
	case VXAIR_FORMAT_Z32_UNORM:
		return VIRGL_FORMAT_Z32_UNORM;
	case VXAIR_FORMAT_Z32_FLOAT:
		return VIRGL_FORMAT_Z32_FLOAT;
	case VXAIR_FORMAT_Z24_UNORM_S8_UINT:
		return VIRGL_FORMAT_Z24_UNORM_S8_UINT;
	case VXAIR_FORMAT_S8_UINT_Z24_UNORM:
		return VIRGL_FORMAT_S8_UINT_Z24_UNORM;
	case VXAIR_FORMAT_Z24X8_UNORM:
		return VIRGL_FORMAT_Z24X8_UNORM;
	case VXAIR_FORMAT_X8Z24_UNORM:
		return VIRGL_FORMAT_X8Z24_UNORM;
	case VXAIR_FORMAT_S8_UINT:
		return VIRGL_FORMAT_S8_UINT;
	case VXAIR_FORMAT_R64_FLOAT:
		return VIRGL_FORMAT_R64_FLOAT;
	case VXAIR_FORMAT_R64G64_FLOAT:
		return VIRGL_FORMAT_R64G64_FLOAT;
	case VXAIR_FORMAT_R64G64B64_FLOAT:
		return VIRGL_FORMAT_R64G64B64_FLOAT;
	case VXAIR_FORMAT_R64G64B64A64_FLOAT:
		return VIRGL_FORMAT_R64G64B64A64_FLOAT;
	case VXAIR_FORMAT_R32_FLOAT:
		return VIRGL_FORMAT_R32_FLOAT;
	case VXAIR_FORMAT_R32G32_FLOAT:
		return VIRGL_FORMAT_R32G32_FLOAT;
	case VXAIR_FORMAT_R32G32B32_FLOAT:
		return VIRGL_FORMAT_R32G32B32_FLOAT;
	case VXAIR_FORMAT_R32G32B32A32_FLOAT:
		return VIRGL_FORMAT_R32G32B32A32_FLOAT;
	default:
		return VIRGL_FORMAT_B8G8R8A8_UNORM;
	}
}

static struct vxair_texture* virgl_create_texture(struct vxair_device* dev, uint32_t w, uint32_t h, vxair_format_t format, const void* pixels) {
	uint32_t res_id = __atomic_fetch_add(&dev->ctx->next_res_id, 1, __ATOMIC_SEQ_CST);
	struct graphic_ioctl_create_resource_cmd tex_cmd = {
	    .desc =
	        {
	            .id = res_id,
	            .type = 2, // GRAPHIC_RESOURCE_TEXTURE_2D
	            .format = (graphic_format_t)vxair_to_virgl_format(format),
	            .bind = (1 << 4), // GRAPHIC_BIND_TEXTURE
	            .width = w,
	            .height = h,
	            .depth = 1,
	            .array_size = 1,
	            .mip_levels = 1,
	            .sample_counts = 1,
	            .ctx_id = dev->active_context_id,
	        },
	    .resource_id = 0,
	};
	if (ioctl(dev->card_fd, GRAPHIC_IOCTL_CREATE_RESOURCE, &tex_cmd) != 0)
		return NULL;

	struct graphic_ioctl_bind_resource_cmd bind_cmd = {
	    .context_id = dev->active_context_id,
	    .resource_id = tex_cmd.resource_id,
	};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_BIND_RESOURCE, &bind_cmd);

	struct graphic_ioctl_attach_backing_cmd att_cmd = {.resource_id = tex_cmd.resource_id};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_ATTACH_BACKING, &att_cmd);

	if (pixels) {
		struct graphic_transfer_cmd transfer = {
		    .context_id = dev->active_context_id,
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
	}

	struct vxair_texture* tex = calloc(1, sizeof(*tex));
	tex->resource_id = tex_cmd.resource_id;
	tex->width = w;
	tex->height = h;
	tex->format = vxair_to_virgl_format(format);
	// tex->sampler_view_id = next_res_id++;
	tex->num_bindings = 0;
	// tex->ctx_bindings[0].ctx_id =

	return tex;
}

static struct vxair_texture* virgl_import_texture(struct vxair_device* dev, uint32_t resource_id, uint32_t w, uint32_t h, vxair_format_t format) {
	struct vxair_texture* tex = calloc(1, sizeof(*tex));
	tex->resource_id = resource_id;
	tex->width = w;
	tex->height = h;
	tex->format = vxair_to_virgl_format(format);
	tex->num_bindings = 0;

	struct graphic_ioctl_bind_resource_cmd bind_cmd = {
	    .context_id = dev->active_context_id,
	    .resource_id = resource_id,
	};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_BIND_RESOURCE, &bind_cmd);

	return tex;
}

static void virgl_texture_update(struct vxair_device* dev, struct vxair_texture* tex, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void* pixels) {
	if (!tex || !pixels)
		return;

	struct graphic_transfer_cmd tex_transfer = {
	    .context_id = dev->active_context_id,
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
	ioctl(dev->card_fd, GRAPHIC_IOCTL_TRANSFER, &tex_transfer);
}

static void virgl_update_cursor(struct vxair_device* dev, uint32_t scanout_id, uint32_t resource_id, uint32_t hot_x, uint32_t hot_y) {
	if (!dev)
		return;
	struct graphic_ioctl_update_cursor_cmd cmd = {.scanout_id = scanout_id, .resource_id = resource_id, .hot_x = hot_x, .hot_y = hot_y};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_UPDATE_CURSOR, &cmd);
}

static void virgl_move_cursor(struct vxair_device* dev, uint32_t scanout_id, uint32_t x, uint32_t y) {
	if (!dev)
		return;
	struct graphic_ioctl_move_cursor_cmd cmd = {.scanout_id = scanout_id, .x = x, .y = y};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_MOVE_CURSOR, &cmd);
}

static struct vxair_texture* virgl_create_cursor(struct vxair_device* dev, uint32_t w, uint32_t h, vxair_format_t format, const void* pixels) {
	uint32_t res_id = __atomic_fetch_add(&dev->ctx->next_res_id, 1, __ATOMIC_SEQ_CST);
	struct graphic_ioctl_create_resource_cmd tex_cmd = {
	    .desc =
	        {
	            .id = res_id,
	            .type = 2, // GRAPHIC_RESOURCE_TEXTURE_2D
	            .format = (graphic_format_t)vxair_to_virgl_format(format),
	            .bind = (1 << 6), // GRAPHIC_BIND_2D
	            .width = w,
	            .height = h,
	            .depth = 1,
	            .array_size = 1,
	            .mip_levels = 1,
	            .sample_counts = 1,
	            .ctx_id = dev->active_context_id,
	        },
	    .resource_id = 0,
	};
	if (ioctl(dev->card_fd, GRAPHIC_IOCTL_CREATE_RESOURCE, &tex_cmd) != 0)
		return NULL;

	struct graphic_ioctl_attach_backing_cmd att_cmd = {.resource_id = tex_cmd.resource_id};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_ATTACH_BACKING, &att_cmd);

	if (pixels) {
		struct graphic_transfer_cmd transfer = {
		    .context_id = dev->active_context_id,
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

		struct graphic_ioctl_resource_flush_cmd flush = {.resource_id = tex_cmd.resource_id, .x = 0, .y = 0, .width = w, .height = h};
		ioctl(dev->card_fd, GRAPHIC_IOCTL_RESOURCE_FLUSH, &flush);
	}

	struct vxair_texture* tex = calloc(1, sizeof(*tex));
	tex->resource_id = tex_cmd.resource_id;
	tex->width = w;
	tex->height = h;
	tex->format = vxair_to_virgl_format(format);
	// tex->sampler_view_id = 0;

	return tex;
}

void virgl_attach_texture(struct vxair_device* dev, struct vxair_texture* tex, vxair_context_t* ctx) {
	uint32_t res_id = tex->resource_id;
	uint32_t ctx_id = ctx->context_id;

	struct graphic_ioctl_bind_resource_cmd bind_cmd = {
	    .context_id = ctx_id,
	    .resource_id = res_id,
	};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_BIND_RESOURCE, &bind_cmd);
}

void virgl_set_scanout(struct vxair_device* dev, struct vxair_context* ctx, int scanout_id) {
	struct graphic_ioctl_set_scanout_cmd scanout_cmd = {.scanout_id = scanout_id, .resource_id = ctx->render_target->resource_id};
	ioctl(dev->card_fd, GRAPHIC_IOCTL_SET_SCANOUT, &scanout_cmd);
}

void virgl_clear_cmd(struct vxair_context* ctx) { ctx->cmd_idx = 0; }

static struct vxair_device_ops virgl_ops = {
    .destroy = virgl_dev_destroy,
    .create_context = virgl_create_context,
    .create_buffer = virgl_create_buffer,
    .destroy_buffer = virgl_destroy_buffer,
    .update_buffer = virgl_buffer_update,
    .create_shader = virgl_create_shader,
    .create_texture = virgl_create_texture,
    .import_texture = virgl_import_texture,
    .create_cursor = virgl_create_cursor,
    .update_texture = virgl_texture_update,
    .update_cursor = virgl_update_cursor,
    .move_cursor = virgl_move_cursor,
    .attach_texture = virgl_attach_texture,
    .set_scanout = virgl_set_scanout,
    .clear_cmd = virgl_clear_cmd,
};

int virgl_backend_init(struct vxair_device* dev) {
	dev->card_fd = open("/dev/dri/card0", O_RDWR);
	if (dev->card_fd < 0)
		return -1;

	// No need for USE_3D, currently kernel does not implement it
	dev->ops = &virgl_ops;
	return 0;
}
