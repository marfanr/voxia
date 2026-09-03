
#include "graphic.h"
#include "ioforge/ioforge.hpp"
#include "memory/kalloc.h"
#include "virtio-gpu/virtio-gpu.hpp"
#include <str.h>

#define ENABLE_DEBUG false

using IOUtils = IOForge::IOUtils;

extern VirtioGpu* g_virtio_gpu;

/* CONTET OPS*/
int virtio_ctx_bind_resource(struct graphic_context* ctx, struct graphic_resource* res) {
	if (!ctx || !res || !g_virtio_gpu)
		return -1;

#if ENABLE_DEBUG
	log("Virtio-GPU API", "Binding resource %d to context %d", res->id, ctx->id);
#endif

	if (g_virtio_gpu->virtio_gpu_ctx_attach_resource(ctx->id, res->id) != 0) {
		log("Virtio-GPU API", "Failed to bind resource %d to context %d", res->id, ctx->id);
		return -1;
	}

#if ENABLE_DEBUG
	log("Virtio-GPU API", "Resource %d bound to context %d successfully", res->id, ctx->id);
#endif
	return 0;
}

int virtio_ctx_transfer(struct graphic_context* ctx, struct graphic_resource* res, struct graphic_transfer_cmd* cmd) {
	if (!ctx || !res || !cmd || !g_virtio_gpu)
		return -1;

	uint32_t bpp = 4;
	switch (res->format) {
	case GRAPHIC_FORMAT_RGB565:
	case GRAPHIC_FORMAT_Z16_UNORM:
	case GRAPHIC_FORMAT_L16_UNORM:
	case GRAPHIC_FORMAT_L8A8_UNORM:
	case GRAPHIC_FORMAT_UYVY:
	case GRAPHIC_FORMAT_YUYV:
		bpp = 2;
		break;
	case GRAPHIC_FORMAT_L8_UNORM:
	case GRAPHIC_FORMAT_A8_UNORM:
	case GRAPHIC_FORMAT_I8_UNORM:
	case GRAPHIC_FORMAT_NONE:
		bpp = 1;
		break;
	default:
		bpp = 4;
		break;
	}

	uint32_t stride = res->width * bpp;
	uint32_t layer_stride = stride * res->height;

	if (res->bind & GRAPHIC_BIND_2D) {
		return g_virtio_gpu->virtio_gpu_transfer_to_host_2d(cmd->offset, cmd->resource_id, cmd->x, cmd->y, cmd->w, cmd->h);
	}

	return g_virtio_gpu->virtio_gpu_transfer_to_host_3d(ctx->id, cmd->resource_id, cmd->offset, cmd->level, cmd->x, cmd->y, cmd->z, cmd->w, cmd->h, cmd->d,
	                                                    stride, layer_stride);
}

int virtio_ctx_transfer2(struct graphic_context* ctx, struct graphic_resource* res, struct graphic_transfer2_cmd* cmd) {
	if (!ctx || !res || !cmd || !g_virtio_gpu)
		return -1;

	return g_virtio_gpu->virtio_gpu_transfer_to_host_3d(ctx->id, cmd->resource_id, cmd->offset, cmd->level, cmd->x, cmd->y, cmd->z, cmd->w, cmd->h, cmd->d,
	                                                    cmd->stride, cmd->layer_stride);
}
int virtio_ctx_submit(struct graphic_context* ctx, const void* commands, size_t size) {
	if (!ctx || !commands || !g_virtio_gpu)
		return -1;
	return g_virtio_gpu->virtio_gpu_submit_3d(ctx->id, (void*)commands, size);
}

int virtio_ctx_transfer_from(struct graphic_context* ctx, struct graphic_resource* res, struct graphic_transfer_cmd* cmd) {
	if (!ctx || !res || !cmd || !g_virtio_gpu)
		return -1;

	uint32_t bpp = 4;
	switch (res->format) {
	case GRAPHIC_FORMAT_RGB565:
	case GRAPHIC_FORMAT_Z16_UNORM:
	case GRAPHIC_FORMAT_L16_UNORM:
	case GRAPHIC_FORMAT_L8A8_UNORM:
	case GRAPHIC_FORMAT_UYVY:
	case GRAPHIC_FORMAT_YUYV:
		bpp = 2;
		break;
	case GRAPHIC_FORMAT_L8_UNORM:
	case GRAPHIC_FORMAT_A8_UNORM:
	case GRAPHIC_FORMAT_I8_UNORM:
	case GRAPHIC_FORMAT_NONE:
		bpp = 1;
		break;
	default:
		bpp = 4;
		break;
	}

	uint32_t stride = res->width * bpp;
	uint32_t layer_stride = stride * res->height;

	return g_virtio_gpu->virtio_gpu_transfer_from_host_3d(ctx->id, cmd->resource_id, cmd->offset, cmd->level, cmd->x, cmd->y, cmd->z, cmd->w, cmd->h,
	                                                      cmd->d, stride, layer_stride);
}

struct graphic_context_ops __gctx_ops = {
    .bind_resource = virtio_ctx_bind_resource,
    .unbind_resource = NULL,
    .submit = virtio_ctx_submit,
    .transfer = virtio_ctx_transfer,
    .transfer2 = virtio_ctx_transfer2,
    .transfer_from = virtio_ctx_transfer_from,
};

/* GRAPHIC OPS*/

extern "C" int virtio_create_context(struct graphic_device* device, struct graphic_context_desc* desc, struct graphic_context** ctx) {
	if (!device || !ctx || !g_virtio_gpu || !desc)
		return -1;

	if (g_virtio_gpu->virtio_gpu_ctx_create(desc->id, desc->nlen, desc->context_init, desc->name) != 0) {
		return -2;
	}
#if ENABLE_DEBUG
	log("Virtio-GPU API", "created context with id %d\n", desc->id);
#endif
	*ctx = (struct graphic_context*)kalloc(sizeof(struct graphic_context) + desc->nlen + 1);
	memset(*ctx, 0, sizeof(struct graphic_context) + desc->nlen + 1);

	(*ctx)->name = (char*)((uintptr_t)*ctx + sizeof(struct graphic_context));
	strncpy((*ctx)->name, desc->name, desc->nlen);
	(*ctx)->context_init = desc->context_init;
	(*ctx)->nlen = desc->nlen;
	(*ctx)->device = device;
	(*ctx)->id = desc->id;
	(*ctx)->ops = &__gctx_ops;
	return 0;
}

static uint32_t virtio_gpu_get_pipe_format(graphic_format_t format) {
	switch (format) {
	case GRAPHIC_FORMAT_NONE:
		return PIPE_FORMAT_NONE;
	case GRAPHIC_FORMAT_R8G8B8A8_UNORM:
		return PIPE_FORMAT_R8G8B8A8_UNORM;
	case GRAPHIC_FORMAT_B8G8R8A8_UNORM:
		return PIPE_FORMAT_B8G8R8A8_UNORM;
	case GRAPHIC_FORMAT_B8G8R8X8_UNORM:
		return PIPE_FORMAT_B8G8R8X8_UNORM;
	case GRAPHIC_FORMAT_A8R8G8B8_UNORM:
		return PIPE_FORMAT_A8R8G8B8_UNORM;
	case GRAPHIC_FORMAT_X8R8G8B8_UNORM:
		return PIPE_FORMAT_X8R8G8B8_UNORM;
	case GRAPHIC_FORMAT_B5G5R5A1_UNORM:
		return PIPE_FORMAT_B5G5R5A1_UNORM;
	case GRAPHIC_FORMAT_B4G4R4A4_UNORM:
		return PIPE_FORMAT_B4G4R4A4_UNORM;
	case GRAPHIC_FORMAT_B5G6R5_UNORM:
		return PIPE_FORMAT_B5G6R5_UNORM;
	case GRAPHIC_FORMAT_R10G10B10A2_UNORM:
		return PIPE_FORMAT_R10G10B10A2_UNORM;
	case GRAPHIC_FORMAT_L8_UNORM:
		return PIPE_FORMAT_L8_UNORM;
	case GRAPHIC_FORMAT_A8_UNORM:
		return PIPE_FORMAT_A8_UNORM;
	case GRAPHIC_FORMAT_I8_UNORM:
		return PIPE_FORMAT_I8_UNORM;
	case GRAPHIC_FORMAT_L8A8_UNORM:
		return PIPE_FORMAT_L8A8_UNORM;
	case GRAPHIC_FORMAT_L16_UNORM:
		return PIPE_FORMAT_L16_UNORM;
	case GRAPHIC_FORMAT_UYVY:
		return PIPE_FORMAT_UYVY;
	case GRAPHIC_FORMAT_YUYV:
		return PIPE_FORMAT_YUYV;
	case GRAPHIC_FORMAT_Z16_UNORM:
		return PIPE_FORMAT_Z16_UNORM;
	case GRAPHIC_FORMAT_Z32_UNORM:
		return PIPE_FORMAT_Z32_UNORM;
	case GRAPHIC_FORMAT_Z32_FLOAT:
		return PIPE_FORMAT_Z32_FLOAT;
	case GRAPHIC_FORMAT_Z24_UNORM_S8_UINT:
		return PIPE_FORMAT_Z24_UNORM_S8_UINT;
	case GRAPHIC_FORMAT_S8_UINT_Z24_UNORM:
		return PIPE_FORMAT_S8_UINT_Z24_UNORM;
	case GRAPHIC_FORMAT_Z24X8_UNORM:
		return PIPE_FORMAT_Z24X8_UNORM;
	case GRAPHIC_FORMAT_X8Z24_UNORM:
		return PIPE_FORMAT_X8Z24_UNORM;
	case GRAPHIC_FORMAT_S8_UINT:
		return PIPE_FORMAT_S8_UINT;
	case GRAPHIC_FORMAT_R64_FLOAT:
		return PIPE_FORMAT_R64_FLOAT;
	case GRAPHIC_FORMAT_R64G64_FLOAT:
		return PIPE_FORMAT_R64G64_FLOAT;
	case GRAPHIC_FORMAT_R64G64B64_FLOAT:
		return PIPE_FORMAT_R64G64B64_FLOAT;
	case GRAPHIC_FORMAT_R64G64B64A64_FLOAT:
		return PIPE_FORMAT_R64G64B64A64_FLOAT;
	case GRAPHIC_FORMAT_R32_FLOAT:
		return PIPE_FORMAT_R32_FLOAT;
	case GRAPHIC_FORMAT_R32G32_FLOAT:
		return PIPE_FORMAT_R32G32_FLOAT;
	case GRAPHIC_FORMAT_R32G32B32_FLOAT:
		return PIPE_FORMAT_R32G32B32_FLOAT;
	case GRAPHIC_FORMAT_R32G32B32A32_FLOAT:
		return PIPE_FORMAT_R32G32B32A32_FLOAT;
	default:
		return PIPE_FORMAT_B8G8R8A8_UNORM;
	}
}

extern "C" int virtio_create_resource(struct graphic_device* device, struct graphic_resource_desc* desc, struct graphic_resource** res) {
	if (!device || !res || !g_virtio_gpu)
		return -1;
#if ENABLE_DEBUG
	log("Virtio-GPU API", "Creating graphic resource for device %d: %dx%d", device->id, desc->width, desc->height);
#endif
	if (device->is_3d && !(desc->bind & GRAPHIC_BIND_2D)) {
		uint32_t target = 0;
		// translate graphic_resource_type_t to PIPE_TEXTURE_*
		switch (desc->type) {
		case GRAPHIC_RESOURCE_TEXTURE_2D:
			target = PIPE_TEXTURE_2D;
			break;
		case GRAPHIC_RESOURCE_TEXTURE_3D:
			target = PIPE_TEXTURE_3D;
			break;
		case GRAPHIC_RESOURCE_TEXTURE_1D:
			target = PIPE_TEXTURE_1D;
			break;
		case GRAPHIC_RESOURCE_BUFFER:
			target = PIPE_BUFFER;
			break;
		default:
			log("Virtio-GPU API", "Unsupported resource type: %d", desc->type);
			return -1;
		}

		// translate graphic_format_t to PIPE_FORMAT_*
		uint32_t bind_flags = 0;
		if (desc->bind & GRAPHIC_BIND_RENDER_TARGET)
			bind_flags |= (1 << 1); // VIRGL_BIND_RENDER_TARGET
		if (desc->bind & GRAPHIC_BIND_SCANOUT)
			bind_flags |= (1 << 18); // VIRGL_BIND_SCANOUT
		if (desc->bind & GRAPHIC_BIND_VERTEX)
			bind_flags |= (1 << 4); // VIRGL_BIND_VERTEX_BUFFER
		if (desc->bind & (1 << 4))      // GRAPHIC_BIND_TEXTURE
			bind_flags |= (1 << 3); // VIRGL_BIND_SAMPLER_VIEW

		if (desc->ctx == NULL) {
			log("Virtio-GPU API", "Failed to create Resource %d: Context is NULL", desc->id);
			return -1;
		}

		uint32_t format = virtio_gpu_get_pipe_format(desc->format);

		// log("Virtio-GPU API", "ctx at 0x%x", desc->ctx);
		// log("Virtio-GPU API",
		//     "Creating Resource %d (3D Render Target) with target %d, "
		//     "format %d, bind_flags %d, width %d, height %d, depth %d, "
		//     "array_size %d, mip_levels %d, sample_counts %d",
		//     desc->id, target, format, bind_flags, desc->width,
		//     desc->height, desc->depth, desc->array_size,
		// desc->mip_levels, desc->sample_counts);

		if (g_virtio_gpu->virtio_gpu_resource_create_3d(desc->ctx->id, desc->id, target, format, bind_flags, desc->width, desc->height, desc->depth,
		                                                desc->array_size, desc->mip_levels, desc->sample_counts, 0) == 0) {
			// log("Virtio-GPU API",
			//     "Resource %d (3D Render Target) created "
			//     "successfully",
			//     desc->id);
		} else {
			log("Virtio-GPU API",
			    "Failed to create Resource %d (3D Render "
			    "Target)",
			    desc->id);
			return -1;
		}
	} else if (desc->bind & GRAPHIC_BIND_2D) {
		if (g_virtio_gpu->virtio_gpu_create_resource(desc->id, desc->width, desc->height) == 0) {
			// log("Virtio-GPU API",
			//     "Resource %d (2D Cursor) created successfully",
			//     desc->id);
		} else {
			log("Virtio-GPU API", "Failed to create Resource %d (2D Cursor)", desc->id);
			return -1;
		}
	}

	// choosing length based on format
	uint32_t bpp = 4;
	switch (desc->format) {
	case GRAPHIC_FORMAT_RGB565:
	case GRAPHIC_FORMAT_Z16_UNORM:
	case GRAPHIC_FORMAT_L16_UNORM:
	case GRAPHIC_FORMAT_L8A8_UNORM:
	case GRAPHIC_FORMAT_UYVY:
	case GRAPHIC_FORMAT_YUYV:
		bpp = 2;
		break;
	case GRAPHIC_FORMAT_L8_UNORM:
	case GRAPHIC_FORMAT_A8_UNORM:
	case GRAPHIC_FORMAT_I8_UNORM:
	case GRAPHIC_FORMAT_NONE:
		bpp = 1;
		break;
	default:
		bpp = 4;
		break;
	}

	uint32_t depth = desc->depth > 0 ? desc->depth : 1;
	uint32_t array_size = desc->array_size > 0 ? desc->array_size : 1;
	uint32_t length = desc->width * desc->height * depth * array_size * bpp;

	// DMA part
	uintptr_t buff_phys = 0;
	auto buff = (uint32_t*)IOForge::IOUtils::DMAAlloc(length, &buff_phys);
	if (!buff) {
		log("Virtio-GPU API", "Failed to allocate framebuffer 2");
		return -1;
	}
	memset(buff, 0, length);

	*res = (struct graphic_resource*)kalloc(sizeof(struct graphic_resource));
	memset(*res, 0, sizeof(struct graphic_resource));

	(*res)->id = desc->id;
	(*res)->type = desc->type;
	(*res)->format = desc->format;
	(*res)->bind = desc->bind;
	(*res)->width = desc->width;
	(*res)->height = desc->height;
	(*res)->depth = desc->depth;

	(*res)->vaddr = (uintptr_t)buff;
	(*res)->paddr = buff_phys;
	(*res)->size = length;
	(*res)->ctx = desc->ctx;

	return 0;
}

int virtio_destroy_resource(struct graphic_device* device, struct graphic_resource* res) {
	if (!device || !res || !g_virtio_gpu)
		return -1;

	log("Virtio-GPU API", "Destroying graphic resource for device %d: id=%d", device->id, res->id);

	// Unreference the resource on the GPU
	int result = g_virtio_gpu->virtio_gpu_unref_resource(res->id);
	if (result != 0) {
		log("Virtio-GPU API", "Failed to unref resource on GPU");
		return -1;
	}

	if (res->vaddr && res->paddr && res->size > 0) {
		IOForge::IOUtils::DMAFree((void*)res->paddr, (void*)res->vaddr, res->size);
	}
	kfree2(res);

	log("Virtio-GPU API", "Graphic resource destroyed: id=%d", res->id);

	return 0;
}

int virtio_destroy_context(struct graphic_device* device, struct graphic_context* ctx) {
	if (!device || !ctx || !g_virtio_gpu)
		return -1;

	log("Virtio-GPU API", "Destroying graphic context: id=%d", ctx->id);

	/* Destroy the context on the GPU */
	int result = g_virtio_gpu->virtio_gpu_ctx_destroy(ctx->id);
	if (result != 0) {
		log("Virtio-GPU API", "Failed to destroy context on GPU");
		return -1;
	}

	kfree2(ctx);

	log("Virtio-GPU API", "Graphic context destroyed: id=%d", ctx->id);

	return 0;
}

int virtio_resource_attach_backing(struct graphic_device* device, struct graphic_resource* res) {
	if (!g_virtio_gpu || !device || !res)
		return -1;

	// log("Virtio-GPU API", "Attaching backing for resource id=%d", res->id);

	struct virtio_gpu_mem_entry entries2[1];
	entries2[0].addr = res->paddr;
	entries2[0].length = res->size;
	entries2[0].padding = 0;

	int result = g_virtio_gpu->virtio_gpu_attach_backing(res->id, 1, entries2);
	if (result != 0) {
		log("Virtio-GPU API", "Failed to attach backing forresource id=%d", res->id);
		return -1;
	}

	return 0;
}

int virtio_scanout_set(struct graphic_device* device, struct graphic_scanout* scanout, struct graphic_resource* res) {
	if (!g_virtio_gpu || !device || !scanout || !res)
		return -1;
	int ret = g_virtio_gpu->virtio_gpu_set_scanout(scanout->id, res->id, 0, 0, scanout->width, scanout->height);

	if (ret == 0) {
		scanout->res = res;
		int flush_ret = g_virtio_gpu->virtio_gpu_resource_flush(res->id, 0, 0, res->width, res->height);

		static uint32_t last_scanout_res_id = 0;
		if (last_scanout_res_id != res->id) {
			// When scanout resource changes (e.g. page-flipping or
			// first initialization), QEMU drops the cursor. We MUST
			// re-apply it AFTER the flush!
			g_virtio_gpu->virtio_gpu_update_cursor(g_virtio_gpu->cursor_res_id_, scanout->id, g_virtio_gpu->cursor_x_, g_virtio_gpu->cursor_y_,
			                                       g_virtio_gpu->cursor_hot_x_, g_virtio_gpu->cursor_hot_y_);
			last_scanout_res_id = res->id;
		}

		return flush_ret;
	}
	return ret;
}

int virtio_resource_flush(struct graphic_device* device, struct graphic_resource* res, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
	if (!g_virtio_gpu || !device || !res)
		return -1;

	return g_virtio_gpu->virtio_gpu_resource_flush(res->id, x, y, width, height);
}

int virtio_update_cursor(struct graphic_device* device, struct graphic_scanout* scanout, struct graphic_resource* res, uint32_t hot_x, uint32_t hot_y) {
	if (!g_virtio_gpu || !device || !scanout || !res)
		return -1;
	return g_virtio_gpu->virtio_gpu_update_cursor(res->id, scanout->id, scanout->x, scanout->y, hot_x, hot_y);
}

int virtio_move_cursor(struct graphic_device* device, struct graphic_scanout* scanout, uint32_t x, uint32_t y) {
	if (!g_virtio_gpu || !device || !scanout)
		return -1;
	return g_virtio_gpu->virtio_gpu_move_cursor(scanout->id, x, y);
}

static struct graphic_device_ops __gdev_ops = {
    .resource_create = virtio_create_resource,
    .create_context = virtio_create_context,
    .resource_attach_backing = virtio_resource_attach_backing,
    .resource_destroy = virtio_destroy_resource,
    .scanout_set = virtio_scanout_set,
    .destroy_context = virtio_destroy_context,
    .resource_flush = virtio_resource_flush,
    .update_cursor = virtio_update_cursor,
    .move_cursor = virtio_move_cursor,
};

/* VIRTIO OPS*/
int VirtioGpu::virtio_gpu_get_display_info() {
	uintptr_t test_phys = 0;
	struct virtio_gpu_ctrl_hdr* test_cmd = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*test_cmd), &test_phys);

	uintptr_t test_resp_phys = 0;
	struct virtio_gpu_resp_display_info* test_resp =
	    (struct virtio_gpu_resp_display_info*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_resp_display_info), &test_resp_phys);

	if (!test_cmd || !test_resp) {
		log(mod, "Failed to allocate test buffers");
		return -1;
	}

	memset(test_cmd, 0, sizeof(*test_cmd));
	test_cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

	memset(test_resp, 0, sizeof(*test_resp));

	// log(mod, "Test buffers allocated: cmd=0x%x, resp=0x%x", test_cmd,
	//     test_resp);

	int result = virtio_gpu_send_command((void*)test_phys, sizeof(*test_cmd), (void*)test_resp_phys, sizeof(*test_resp));

	if (result == 0) {
		log(mod, "GPU communication test PASSED");

		// Get existing graphic device created by graphic.c (for /dev/dri/card0)
		auto graphic = graphic_get_device(0);
		if (!graphic) {
			log(mod, "Failed to get graphic device 0, creating new one");
			graphic = create_graphic_device();
		}
		graphic->ops = &__gdev_ops;
		graphic->is_3d = true;

		if (test_resp->hdr.type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
			int enabled_count = 0;
			for (int i = 0; i < 16; i++) {
				if (test_resp->pmodes[i].enabled) {
					log(mod,
					    "Scanout %d: %dx%d at "
					    "(%d,%d)",
					    i, test_resp->pmodes[i].rect.width, test_resp->pmodes[i].rect.height, test_resp->pmodes[i].rect.x,
					    test_resp->pmodes[i].rect.y);
					enabled_count++;

					graphic_alloc_scanout(i, graphic, test_resp->pmodes[i].rect.width, test_resp->pmodes[i].rect.height,
					                      test_resp->pmodes[i].rect.x, test_resp->pmodes[i].rect.y);
				}
			}
			if (enabled_count == 0) {
				// Fallback: GPU online tapi belum melaporkan scanout aktif.
				// Alokasi scanout default 1280x720 agar splashScreenTask
				// tidak abort saat menunggu scanout_list.
				log(mod, "No enabled display modes found, "
				         "allocating default 1366x768 scanout.");
				graphic_alloc_scanout(0, graphic, 1366, 768, 0, 0);
			}
		} else {
			log(mod, "Unexpected response type: 0x%x", test_resp->hdr.type);
			// Fallback scanout agar device tetap usable
			graphic_alloc_scanout(0, graphic, 1366, 768, 0, 0);
		}
	} else {
		log(mod, "GPU communication test FAILED");
	}
	IOUtils::DMAFree((void*)test_phys, (void*)test_cmd, sizeof(*test_cmd));
	IOUtils::DMAFree((void*)test_resp_phys, (void*)test_resp, sizeof(*test_resp));

	return 0;
}

int VirtioGpu::virtio_gpu_unref_resource(uint32_t resource_id) {

	uintptr_t cmd_phys = 0;
	struct virtio_gpu_resource_unref* cmd = (struct virtio_gpu_resource_unref*)IOUtils::DMAAlloc(sizeof(*cmd), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = 0;
	cmd->hdr.padding = 0;
	cmd->padding = 0;
	cmd->resource_id = resource_id;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(*cmd), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to unref resource");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(*cmd));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Resource unref failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(*cmd));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(*cmd));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_create_resource(uint32_t resource_id, uint32_t width, uint32_t height) {

	uintptr_t cmd_phys = 0;
	struct virtio_gpu_resource_create_2d* cmd = (struct virtio_gpu_resource_create_2d*)IOUtils::DMAAlloc(sizeof(*cmd), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = 0;
	cmd->hdr.padding = 0;
	cmd->resource_id = resource_id;
	cmd->format = 2; // VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM
	cmd->width = width;
	cmd->height = height;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	if (!cmd || !resp) {
		log(mod, "Failed to allocate create resource command buffers");
		if (cmd)
			IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(*cmd));
		if (resp)
			IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	memset(resp, 0, sizeof(*resp));

	log(mod, "Creating resource %d: %dx%d", resource_id, width, height);
	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(*cmd), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to create resource");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(*cmd));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Resource create failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(*cmd));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(*cmd));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	log(mod, "Resource %d created: %dx%d", resource_id, width, height);
	return 0;
}

int VirtioGpu::virtio_gpu_attach_backing(uint32_t resource_id, uint32_t nr_entries, struct virtio_gpu_mem_entry* entries) {
	uintptr_t cmd_phys = 0;
	size_t cmd_size = sizeof(struct virtio_gpu_resource_attach_backing) + sizeof(struct virtio_gpu_mem_entry) * nr_entries;
	struct virtio_gpu_resource_attach_backing* cmd = (struct virtio_gpu_resource_attach_backing*)IOUtils::DMAAlloc(cmd_size, &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = 0;
	cmd->hdr.padding = 0;
	cmd->resource_id = resource_id;
	cmd->nr_entries = nr_entries;
	memcopy(cmd->entries, entries, sizeof(struct virtio_gpu_mem_entry) * nr_entries);

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	if (!cmd || !resp) {
		log(mod, "Failed to allocate attach backing command buffers");
		if (cmd)
			IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, cmd_size);
		if (resp)
			IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, cmd_size, (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to create resource");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, cmd_size);
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Resource create failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, cmd_size);
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, cmd_size);
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_set_scanout(uint32_t scanout_id, uint32_t resource_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
	uintptr_t cmd_phys = 0;
	struct virtio_gpu_set_scanout* cmd = (struct virtio_gpu_set_scanout*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_set_scanout), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = 0;
	cmd->hdr.padding = 0;
	cmd->resource_id = resource_id;

	cmd->rect.x = x;
	cmd->rect.y = y;
	cmd->rect.width = width;
	cmd->rect.height = height;

	cmd->scanout_id = scanout_id;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_set_scanout), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to create resource");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_set_scanout));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Resource create failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_set_scanout));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}
	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_set_scanout));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_transfer_to_host_2d(uint64_t offset, uint32_t resource_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
	uintptr_t cmd_phys = 0;
	struct virtio_gpu_transfer_to_host_2d* cmd =
	    (struct virtio_gpu_transfer_to_host_2d*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_transfer_to_host_2d), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = 0;
	cmd->hdr.padding = 0;
	cmd->resource_id = resource_id;

	cmd->rect.x = x;
	cmd->rect.y = y;
	cmd->rect.width = width;
	cmd->rect.height = height;

	cmd->offset = offset;
	cmd->padding = 0;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_transfer_to_host_2d), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to create resource");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_transfer_to_host_2d));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Resource create failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_transfer_to_host_2d));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}
	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_transfer_to_host_2d));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_resource_flush(uint32_t resource_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {

	uintptr_t cmd_phys = 0;
	struct virtio_gpu_resource_flush* cmd = (struct virtio_gpu_resource_flush*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_resource_flush), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = 0;
	cmd->hdr.padding = 0;
	cmd->resource_id = resource_id;

	cmd->rect.x = x;
	cmd->rect.y = y;
	cmd->rect.width = width;
	cmd->rect.height = height;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_resource_flush), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to create resource");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_resource_flush));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Resource create failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_resource_flush));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}
	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_resource_flush));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_ctx_create(uint32_t ctx_id, uint32_t nlen, uint32_t context_init, char* debug_name) {
	uintptr_t cmd_phys = 0;
	struct virtio_gpu_ctx_create* cmd = (struct virtio_gpu_ctx_create*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_ctx_create), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_CTX_CREATE;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = ctx_id;
	cmd->hdr.padding = 0;
	cmd->nlen = nlen;
	cmd->context_init = context_init;
	memcopy(cmd->debug_name, debug_name, nlen);

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_ctx_create), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to create resource");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_ctx_create));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Resource create failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_ctx_create));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}
	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_ctx_create));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_ctx_destroy(uint32_t ctx_id) {
	uintptr_t cmd_phys = 0;
	struct virtio_gpu_ctx_destroy* cmd = (struct virtio_gpu_ctx_destroy*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_ctx_destroy), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_CTX_DESTROY;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = ctx_id;
	cmd->hdr.padding = 0;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_ctx_destroy), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to destroy context");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_ctx_destroy));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Context destroy failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_ctx_destroy));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_ctx_destroy));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_resource_create_3d(uint32_t ctx_id, uint32_t resource_id, uint32_t target, uint32_t format, uint32_t bind, uint32_t width,
                                             uint32_t height, uint32_t depth, uint32_t array_size, uint32_t last_level, uint32_t nr_samples, uint32_t flags) {

	uintptr_t cmd_phys = 0;
	struct virtio_gpu_resource_create_3d* cmd =
	    (struct virtio_gpu_resource_create_3d*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_resource_create_3d), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = ctx_id;
	cmd->hdr.padding = 0;
	cmd->resource_id = resource_id;
	cmd->target = target;
	cmd->format = format;
	cmd->bind = bind;
	cmd->width = width;
	cmd->height = height;
	cmd->depth = depth;
	cmd->array_size = array_size;
	cmd->last_level = last_level;
	cmd->nr_samples = nr_samples;
	cmd->flags = flags;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_resource_create_3d), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to create resource");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_resource_create_3d));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Resource create failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_resource_create_3d));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}
	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_resource_create_3d));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}
int VirtioGpu::virtio_gpu_ctx_attach_resource(uint32_t ctx_id, uint32_t resource_id) {
	uintptr_t cmd_phys = 0;
	struct virtio_gpu_ctx_resource* cmd = (struct virtio_gpu_ctx_resource*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_ctx_resource), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = ctx_id;
	cmd->hdr.padding = 0;
	cmd->resource_id = resource_id;
	cmd->padding = 0;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_ctx_resource), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to attach resource to context");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_ctx_resource));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Context attach resource failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_ctx_resource));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}
	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_ctx_resource));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_submit_3d(uint32_t ctx_id, void* cmd_buf, uint32_t cmd_buf_size) {
	uintptr_t cmd_phys = 0;
	size_t total_size = sizeof(struct virtio_gpu_cmd_submit) + cmd_buf_size;
	struct virtio_gpu_cmd_submit* cmd = (struct virtio_gpu_cmd_submit*)IOUtils::DMAAlloc(total_size, &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_SUBMIT_3D;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = ctx_id;
	cmd->hdr.padding = 0;
	cmd->size = cmd_buf_size;
	cmd->padding = 0;

	// Salin data command stream 3D tepat di belakang header command
	memcopy((char*)cmd + sizeof(struct virtio_gpu_cmd_submit), cmd_buf, cmd_buf_size);

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, total_size, (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to submit 3D command");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, total_size);
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "3D submit failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, total_size);
		return -1;
	}

	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, total_size);
	return 0;
}

int VirtioGpu::virtio_gpu_transfer_to_host_3d(uint32_t ctx_id, uint32_t resource_id, uint64_t offset, uint32_t level, uint32_t x, uint32_t y, uint32_t z,
                                              uint32_t w, uint32_t h, uint32_t d, uint32_t stride, uint32_t layer_stride) {
	uintptr_t cmd_phys = 0;
	struct virtio_gpu_transfer_host_3d* cmd = (struct virtio_gpu_transfer_host_3d*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_transfer_host_3d), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = ctx_id;
	cmd->hdr.padding = 0;

	cmd->resource_id = resource_id;
	cmd->level = level;
	cmd->offset = offset;
	cmd->stride = stride;
	cmd->layer_stride = layer_stride;

	cmd->box.x = x;
	cmd->box.y = y;
	cmd->box.z = z;
	cmd->box.w = w;
	cmd->box.h = h;
	cmd->box.d = d;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_transfer_host_3d), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to send TRANSFER_TO_HOST_3D command");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_transfer_host_3d));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "TRANSFER_TO_HOST_3D failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_transfer_host_3d));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_transfer_host_3d));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_transfer_from_host_3d(uint32_t ctx_id, uint32_t resource_id, uint64_t offset, uint32_t level, uint32_t x, uint32_t y, uint32_t z,
                                                uint32_t w, uint32_t h, uint32_t d, uint32_t stride, uint32_t layer_stride) {
	uintptr_t cmd_phys = 0;
	struct virtio_gpu_transfer_host_3d* cmd = (struct virtio_gpu_transfer_host_3d*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_transfer_host_3d), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = ctx_id;
	cmd->hdr.padding = 0;

	cmd->resource_id = resource_id;
	cmd->level = level;
	cmd->offset = offset;
	cmd->stride = stride;
	cmd->layer_stride = layer_stride;

	cmd->box.x = x;
	cmd->box.y = y;
	cmd->box.z = z;
	cmd->box.w = w;
	cmd->box.h = h;
	cmd->box.d = d;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_transfer_host_3d), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to send TRANSFER_FROM_HOST_3D command");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_transfer_host_3d));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "TRANSFER_FROM_HOST_3D failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_transfer_host_3d));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_transfer_host_3d));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

int VirtioGpu::virtio_gpu_resource_create_blob(uint32_t ctx_id, uint32_t resource_id, uint32_t blob_mem, uint32_t blob_flags, uint64_t blob_id, uint64_t size) {

	uintptr_t cmd_phys = 0;
	struct virtio_gpu_resource_create_blob* cmd =
	    (struct virtio_gpu_resource_create_blob*)IOUtils::DMAAlloc(sizeof(struct virtio_gpu_resource_create_blob), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = ctx_id;
	cmd->hdr.padding = 0;

	cmd->resource_id = resource_id;
	cmd->blob_mem = blob_mem;
	cmd->blob_flags = blob_flags;
	cmd->nr_entries = 0; // We will attach backing later separately
	cmd->blob_id = blob_id;
	cmd->size = size;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)IOUtils::DMAAlloc(sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(struct virtio_gpu_resource_create_blob), (void*)resp_phys, sizeof(*resp)) < 0) {
		log(mod, "Failed to send RESOURCE_CREATE_BLOB command");
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_resource_create_blob));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "RESOURCE_CREATE_BLOB failed: 0x%x", resp->type);
		IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_resource_create_blob));
		IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
		return -1;
	}

	IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(struct virtio_gpu_resource_create_blob));
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

#undef ENABLE_DEBUG