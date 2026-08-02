#ifndef __VCOMP_H__
#define __VCOMP_H__

#include "freetype/freetype.h"
#include <stdbool.h>
#include <stdint.h>
#include <vxair.h>

/* ── Utilities ─────────────────────────────────────────────────── */

typedef union rgba8 {
	struct {
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};
	uint32_t hex;
} rgba8_t;

inline rgba8_t rgba_from_hex(uint32_t hex) {
	rgba8_t c;
	c.r = (hex >> 24) & 0xFF;
	c.g = (hex >> 16) & 0xFF;
	c.b = (hex >> 8) & 0xFF;
	c.a = hex & 0xFF;
	return c;
}

inline uint32_t rgba_to_hex(rgba8_t c) {
	return (c.r << 24) | (c.g << 16) | (c.b << 8) | c.a;
}

uint32_t* load_bmp32_raw(const char* filepath, uint32_t* out_w,
                         uint32_t* out_h);

// font struct removed, using vxui.h directly
#include <vxui.h>

/* ── Loader worker (background I/O + texture upload) ──────────── */

typedef struct {
	vxair_device_t* dev;
	vxair_context_t* ctx;
	vxair_texture_t* tex; // wallpaper texture, set saat selesai upload
	volatile int early_phase_done;
	uint32_t img_w;
	uint32_t img_h;

	float screen_w;
	float screen_h;

	vxair_texture_t* font_tex;
	vxui_font_t* font;
	vxui_text_renderer_t* text_renderer;

	FT_Face face;
} worker_args_t;

/* ── Splash scene ──────────────────────────────────────────────── */

typedef struct {
	vxair_context_t* ctx;
	vxair_buffer_t* vbo_logo;
	vxair_buffer_t* vbo_loading;
	vxair_texture_t* tex_logo;
	vxair_texture_t* tex_loading;
	vxair_shader_t* vs;
	vxair_shader_t* fs;
	/* state animasi */
	float angle;
	float Sx, Sy, Tx, Ty;
	float load_cx, load_cy;
	float mvp_logo[16];
	rgba8_t bg_color;
	/* scissor box untuk spinner region */
	int scissor_x, scissor_y, scissor_w, scissor_h;
} splash_scene_t;

splash_scene_t* splash_init(vxair_device_t* dev, vxair_context_t* ctx,
                            worker_args_t* args);
void splash_render(splash_scene_t* s);
void splash_destroy(splash_scene_t* s);

typedef struct {
	vxair_context_t* ctx;
	vxair_buffer_t* vbo;
	vxair_shader_t* vs;
	vxair_shader_t* fs;
	vxair_texture_t* cursor_tex;
	rgba8_t bg_color;
} wallpaper_scene_t;

wallpaper_scene_t* wallpaper_init(vxair_device_t* dev, vxair_context_t* ctx,
                                  worker_args_t* args);
void wallpaper_render(wallpaper_scene_t* s, worker_args_t* args, bool do_clear);

//  Window
typedef struct {
	vxair_context_t* ctx;
	vxair_buffer_t* vbo;
	vxair_shader_t* vs;
	vxair_shader_t* fs;
	rgba8_t bg_color;

	float x;
	float y;
	float w;
	float h;

	float titlebar_height;

	// shadow
	vxair_buffer_t* shadow_vbo;
	vxair_shader_t* shadow_vs;
	vxair_shader_t* shadow_fs;
	rgba8_t shadow_color;

	uint32_t alpha_blend_id;
	vxair_texture_t* fb_tex; // framebuffer texture (chessboard placeholder)

	int use_texture;
} window_scene_t;

window_scene_t* window_init(vxair_device_t* dev, vxair_context_t* ctx,
                            worker_args_t* args);
void window_render(window_scene_t* s, worker_args_t* args);

void check_titlebar_click(int px, int py);

// utils
uint64_t time_ns();
void sleep_ns(uint64_t ns);

// temp
extern int need_rerender;
extern float g_screen_w;
extern float g_screen_h;

extern int active_client_fd;

// debug
void sock_log(const char* fmt, ...);

#endif // __VCOMP_H__agy