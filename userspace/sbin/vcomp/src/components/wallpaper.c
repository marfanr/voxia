#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <vcomp.h>
#include <vxair.h>

/* Cursor helper — load & upload 64x64 BMP */
static vxair_texture_t* load_cursor(vxair_device_t* dev) {
	uint32_t w, h;
	uint32_t* px = load_bmp32_raw("/usr/shared/cursor.bmp", &w, &h);
	if (!px) {
		printf("FAILED TO LOAD /usr/shared/cursor.bmp\n");
		return NULL;
	}

	uint32_t* padded = calloc(64 * 64, sizeof(uint32_t));
	for (uint32_t y = 0; y < h && y < 64; y++)
		for (uint32_t x = 0; x < w && x < 64; x++)
			padded[y * 64 + x] = px[y * w + x];
	free(px);

	// for (uint32_t y = 0; y < h && y < 64; y++)
	// 	for (uint32_t x = 0; x < w && x < 64; x++)
	// 		if ()

	vxair_texture_t* tex =
	    vxair_cursor_create(dev, 64, 64, VXAIR_FORMAT_RGBA8, padded);
	free(padded);
	return tex;
}

static const char* WALL_VS = "VERT\n"
                             "DCL IN[0]\n"
                             "DCL IN[1]\n"
                             "DCL OUT[0], POSITION\n"
                             "DCL OUT[1], GENERIC[0]\n"
                             "DCL CONST[0..1]\n"
                             "DCL TEMP[0]\n"
                             "MUL TEMP[0].xy, IN[0].xyxy, CONST[0].xyxy\n"
                             "ADD TEMP[0].xy, TEMP[0].xyxy, CONST[0].zwzw\n"
                             "MOV TEMP[0].z, CONST[1].xxxx\n"
                             "MOV TEMP[0].w, CONST[1].yyyy\n"
                             "MOV OUT[0], TEMP[0]\n"
                             "MOV OUT[1], IN[1]\n"
                             "END\n";

static const char* WALL_FS = "FRAG\n"
                             "DCL IN[0], GENERIC[0], PERSPECTIVE\n"
                             "DCL OUT[0], COLOR\n"
                             "DCL SAMP[0]\n"
                             "DCL SVIEW[0], 2D, FLOAT\n"
                             "TEX OUT[0], IN[0], SAMP[0], 2D\n"
                             "END\n";

// static vxair_context_t* test_ctx;

wallpaper_scene_t* wallpaper_init(vxair_device_t* dev, vxair_context_t* ctx,
                                  worker_args_t* args) {
	wallpaper_scene_t* s = calloc(1, sizeof(wallpaper_scene_t));

	s->ctx = ctx;
	s->vs = vxair_shader_create(dev, VXAIR_SHADER_VERTEX, WALL_VS);
	s->fs = vxair_shader_create(dev, VXAIR_SHADER_FRAGMENT, WALL_FS);

	// test_ctx = vxair_context_create(dev, 1280, 720);

	/* Full-screen quad */
	float verts[] = {
	    0.0f,           0.0f,           0.0f, 0.0f,
	    0.0f,           args->screen_h, 0.0f, 1.0f,
	    args->screen_w, args->screen_h, 1.0f, 1.0f,
	    0.0f,           0.0f,           0.0f, 0.0f,
	    args->screen_w, args->screen_h, 1.0f, 1.0f,
	    args->screen_w, 0.0f,           1.0f, 0.0f,
	};
	s->vbo = vxair_buffer_create(dev, sizeof(verts), verts);

	/* Cursor */
	s->cursor_tex = load_cursor(dev);
	if (s->cursor_tex) {
		vxair_update_cursor(
		    dev, 0, vxair_texture_get_resource_id(s->cursor_tex), 10,
		    7);
	}
	s->bg_color = rgba_from_hex(0x282F28FF);

	return s;
}

void wallpaper_render(wallpaper_scene_t* s, worker_args_t* args, bool do_clear) {
	vxair_cmd_set_viewport(s->ctx, 0.0f, 0.0f, args->screen_w,
	                       args->screen_h, 0.0f, 0.0f);
	vxair_cmd_bind_shader(s->ctx, s->vs);
	vxair_cmd_bind_shader(s->ctx, s->fs);

	float uniform[8] = {
	    2.0f / args->screen_w,
	    2.0f / args->screen_h,
	    -1.0f,
	    -1.0f,
	    // offset
	    0.0f,
	    1.0f,
	    0.0f,
	    0.0f,
	};
	vxair_cmd_set_constant_buffer(s->ctx, VXAIR_SHADER_VERTEX, 0,
	                              sizeof(uniform), uniform);

	vxair_vertex_element_t elems[2] = {
	    {0, VXAIR_VERTEX_FORMAT_FLOAT2},
	    {8, VXAIR_VERTEX_FORMAT_FLOAT2},
	};
	vxair_cmd_bind_vertex_elements(s->ctx, 2, elems);
	vxair_cmd_bind_vertex_buffer(s->ctx, s->vbo, 4 * sizeof(float));

	rgba8_t* bg = &s->bg_color;
	if (do_clear)
		vxair_cmd_clear(s->ctx, bg->r / 255.0f, bg->g / 255.0f, bg->b / 255.0f,
		                bg->a / 255.0f);

	if (args->early_phase_done) {
		// vxair_texture_attach(args->dev, args->tex, test_ctx);
		// vxair_cmd_bind_texture(test_ctx, 0, args->tex);
		vxair_cmd_bind_texture(s->ctx, 0, args->tex);
		vxair_cmd_draw_arrays(s->ctx, VXAIR_PRIMITIVE_TRIANGLES, 0, 6);
	}
}
