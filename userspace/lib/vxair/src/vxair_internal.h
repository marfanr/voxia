#ifndef VXAIR_INTERNAL_H
#define VXAIR_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <vxair.h>

struct vxair_device;
struct vxair_context;
struct vxair_buffer;
struct vxair_shader;

struct vxair_device_ops {
	void (*destroy)(struct vxair_device* dev);
	struct vxair_context* (*create_context)(struct vxair_device* dev,
	                                        uint32_t w, uint32_t h);
	struct vxair_buffer* (*create_buffer)(struct vxair_device* dev,
	                                      size_t size, const void* data);
	void (*destroy_buffer)(struct vxair_device* dev, struct vxair_buffer* buf);
	void (*update_buffer)(struct vxair_device* dev, struct vxair_buffer* buf,
	                      size_t offset, size_t size, const void* data);
	struct vxair_shader* (*create_shader)(struct vxair_device* dev,
	                                      vxair_shader_type_t type,
	                                      const char* src);
	struct vxair_texture* (*create_texture)(struct vxair_device* dev,
	                                        uint32_t w, uint32_t h,
	                                        vxair_format_t format,
	                                        const void* pixels);
	struct vxair_texture* (*import_texture)(struct vxair_device* dev,
	                                        uint32_t resource_id,
	                                        uint32_t w, uint32_t h,
	                                        vxair_format_t format);
	struct vxair_texture* (*create_cursor)(struct vxair_device* dev,
	                                       uint32_t w, uint32_t h,
	                                       vxair_format_t format,
	                                       const void* pixels);
	void (*update_texture)(struct vxair_device* dev,
	                       struct vxair_texture* tex, uint32_t x,
	                       uint32_t y, uint32_t w, uint32_t h,
	                       const void* pixels);
	void (*update_cursor)(struct vxair_device* dev, uint32_t scanout_id,
	                      uint32_t resource_id, uint32_t hot_x,
	                      uint32_t hot_y);
	void (*move_cursor)(struct vxair_device* dev, uint32_t scanout_id,
	                    uint32_t x, uint32_t y);
	void (*attach_texture)(struct vxair_device* dev,
	                       struct vxair_texture* tex, vxair_context_t* ctx);
	void (*set_scanout)(struct vxair_device* dev, struct vxair_context* ctx,
	                    int scanout_id);
	void (*clear_cmd)(struct vxair_context* ctx);
};

struct vxair_context_ops {
	void (*destroy)(struct vxair_context* ctx);
	void (*cmd_set_viewport)(struct vxair_context* ctx, float x, float y,
	                         float w, float h, float min_z, float max_z);
	void (*cmd_clear)(struct vxair_context* ctx, float r, float g, float b,
	                  float a);
	void (*cmd_bind_shader)(struct vxair_context* ctx,
	                        struct vxair_shader* shader);
	void (*cmd_bind_vertex_buffer)(struct vxair_context* ctx,
	                               struct vxair_buffer* vbo,
	                               uint32_t stride);
	void (*cmd_draw_arrays)(struct vxair_context* ctx,
	                        vxair_primitive_t mode, uint32_t start,
	                        uint32_t count);
	void (*cmd_bind_texture)(struct vxair_context* ctx, uint32_t slot,
	                         struct vxair_texture* tex);
	void (*cmd_set_constant_buffer)(struct vxair_context* ctx,
	                                vxair_shader_type_t shader_type,
	                                uint32_t index, uint32_t size,
	                                const void* data);
	void (*cmd_bind_vertex_elements)(
	    struct vxair_context* ctx, uint32_t num_elements,
	    const vxair_vertex_element_t* elements);
	void (*submit_and_present)(struct vxair_context* ctx);
	void (*read_pixels)(struct vxair_context* ctx, uint32_t x, uint32_t y,
	                    uint32_t w, uint32_t h, void* pixels);
	void (*cmd_set_sampler_filter)(struct vxair_context* ctx, uint32_t slot,
	                               vxair_filter_t min_filter,
	                               vxair_filter_t mag_filter);
	uint32_t (*create_alpha_blend)(vxair_context_t* ctx);
	void (*bind_blend)(vxair_context_t* ctx, uint32_t blend_id);
	void (*cmd_set_scissor)(vxair_context_t* ctx, uint32_t x, uint32_t y,
	                        uint32_t width, uint32_t height);
	void (*cmd_disable_scissor)(vxair_context_t* ctx);
};

struct vxair_device {
	int card_fd;
	vxair_backend_t backend;
	struct vxair_device_ops* ops;
	uint32_t active_context_id;
	vxair_context_t* ctx;
};

// TODO: make it dynamic, auto increase if more than 1024
#define VXAIR_CMD_BUF_SIZE 8192
#define VXAIR_MAX_VE_CACHE 24

struct vxair_context {
	struct vxair_device* dev;
	uint32_t context_id;
	struct vxair_context_ops* ops;

	uint32_t cmd_buffer[VXAIR_CMD_BUF_SIZE];
	int cmd_idx;

	struct vxair_buffer* render_target;
	uint32_t scanout_res_id;
	uint32_t kctx_id; // Kernel Context ID
	void* cpu_buffer;

	struct vxair_buffer* bound_vbo;
	struct vxair_texture* bound_tex;
	uint32_t linear_sampler_id;
	uint32_t nearest_sampler_id;

	/* scissor state */
	bool scissor_enabled;
	uint32_t scissor_x;
	uint32_t scissor_y;
	uint32_t scissor_width;
	uint32_t scissor_height;

	/* vertex elements cache to avoid creating new VE objects every frame */
	struct {
		uint32_t hash;
		uint32_t ve_id;
	} ve_cache[VXAIR_MAX_VE_CACHE];
	uint32_t ve_obj_id;  // per-context VE object ID counter (starts at 1000)

	/* Per-context GPU resource ID counter, starts at 30 */
	uint32_t next_res_id;
};

struct vxair_buffer {
	uint32_t resource_id;
	size_t size;
	void* mapped_ptr;
	void* cpu_data;
};

struct vxair_shader {
	uint32_t handle_id;
	vxair_shader_type_t type;
	char* src;
	int created_in_ctx;
};

#define VXAIR_MAX_TEX_CTX_BINDINGS 8

struct vxair_texture {
	uint32_t resource_id;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	void* cpu_pixels;

	// uint32_t sampler_view_id;
	struct {
		uint32_t ctx_id;
		uint32_t sampler_view_id;
	} ctx_bindings[VXAIR_MAX_TEX_CTX_BINDINGS];
	int num_bindings;
	// int sampler_view_created;
};

#endif // VXAIR_INTERNAL_H
