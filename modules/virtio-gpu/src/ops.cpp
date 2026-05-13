
#include "virtio-gpu/virtio-gpu.hpp"
#include <str.h>

int VirtioGpu::virtio_gpu_get_display_info() {
	uintptr_t cmd_phys = 0;
	struct virtio_gpu_ctrl_hdr* cmd =
		(struct virtio_gpu_ctrl_hdr*) IOUtils::DMAAlloc(sizeof(*cmd),
								&cmd_phys);

	cmd->ctx_id = 0;
	cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
	cmd->flags = 0;
	cmd->fence_id = 0;
	cmd->padding = 0;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_resp_display_info* resp =
		(struct virtio_gpu_resp_display_info*) IOUtils::DMAAlloc(
			sizeof(*resp), &resp_phys);

	memset(resp, 0, sizeof(*resp));

	log(mod, "Sending GET_DISPLAY_INFO command");
	if (virtio_gpu_send_command((void*) cmd_phys, sizeof(*cmd),
				    (void*) resp_phys, sizeof(*resp))
	    < 0) {
		log(mod, "Failed to send GET_DISPLAY_INFO command");
		return -1;
	}

	if (resp->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
		log(mod, "Bad response type: 0x%x (expected 0x%x)",
		    resp->hdr.type, VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
		return -1;
	}

	log(mod, "Display info received:");
	int enabled_count = 0;
	for (int i = 0; i < 16; i++) {
		if (resp->pmodes[i].enabled) {
			log(mod, "  Scanout %d: %dx%d at (%d,%d)", i,
			    resp->pmodes[i].rect.width,
			    resp->pmodes[i].rect.height, resp->pmodes[i].rect.x,
			    resp->pmodes[i].rect.y);
			enabled_count++;
		}
	}

	if (enabled_count == 0) {
		log(mod, "No enabled display modes found");
	}

	return 0;
}

int VirtioGpu::virtio_gpu_create_resource(uint32_t resource_id, uint32_t width,
					  uint32_t height) {

	uintptr_t cmd_phys = 0;
	struct virtio_gpu_resource_create_2d* cmd =
		(struct virtio_gpu_resource_create_2d*) IOUtils::DMAAlloc(
			sizeof(*cmd), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = 0;
	cmd->hdr.padding = 0;
	cmd->resource_id = resource_id;
	cmd->format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
	cmd->width = width;
	cmd->height = height;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp =
		(struct virtio_gpu_ctrl_hdr*) IOUtils::DMAAlloc(sizeof(*resp),
								&resp_phys);

	memset(resp, 0, sizeof(*resp));

	log(mod, "Creating resource %d: %dx%d", resource_id, width, height);
	if (virtio_gpu_send_command((void*) cmd_phys, sizeof(*cmd),
				    (void*) resp_phys, sizeof(*resp))
	    < 0) {
		log(mod, "Failed to create resource");
		return -1;
	}

	if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
		log(mod, "Resource create failed: 0x%x", resp->type);
		return -1;
	}

	log(mod, "Resource %d created: %dx%d", resource_id, width, height);
	return 0;
}