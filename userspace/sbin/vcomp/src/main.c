#include "vxair.h"
#include <errno.h>
#include <fcntl.h>
#include <ft2build.h>
#include <input.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <vcomp.h>
#include <vxair.h>

struct message {
	uint32_t type;
	uint32_t len;
	uint32_t data[];
};

enum vcomp_message_type {
	VCOMP_CREATE_WINDOW = 1,
	VCOMP_SUBMIT_RESOURCE,
	VCOMP_DESTROY_WINDOW,
	VCOMP_INPUT_REQUEST,
	VCOMP_INPUT_RESPONSE,
	VCOMP_SET_WINDOW,
	VCOMP_LOG,
	VCOMP_REPORT_KEYMAP
};

// Replaced by vxui_text API
static void* loader_worker(void* arg) {
	worker_args_t* args = (worker_args_t*)arg;

	vxui_font_t* font = vxui_font_create(args->dev, "/usr/shared/fonts/roboto.ttf", 12, 32, 126, FONT_STYLE_REGULAR);
	if (!font) {
		return 0;
	}
	args->font = font;

	uint32_t* px = load_bmp32_raw("/usr/shared/wallpaper.bmp", &args->img_w, &args->img_h);
	if (!px) {
		printf("Gagal memuat wallpaper.bmp\n");
		return NULL;
	}
	args->tex = vxair_texture_create(args->dev, args->img_w, args->img_h, VXAIR_FORMAT_RGBA8, px);
	free(px);

	printf("Wallpaper siap.\n");

	args->text_renderer = vxui_text_renderer_create(args->dev, args->screen_w, args->screen_h);

	args->early_phase_done = 1;
	return NULL;
}

#define SOCK_DBG_LINES 8
#define SOCK_DBG_LEN 128
static char sock_dbg[SOCK_DBG_LINES][SOCK_DBG_LEN];
static int sock_dbg_idx = 0;

void sock_log(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(sock_dbg[sock_dbg_idx % SOCK_DBG_LINES], SOCK_DBG_LEN, fmt, ap);
	va_end(ap);
	sock_dbg_idx++;
}

// TODO: add client_id to each window
uint32_t client_id = 0;
int active_client_fd = -1;

void* socket_accept_loop(void* arg) {
	int sock = (int)(intptr_t)arg;
	sock_log("accept loop started fd=%d", sock);

	while (1) {
		int fd = accept(sock, NULL, NULL);
		if (fd < 0) {
			if (errno == EAGAIN) {
				usleep(1000);
				continue;
			}
			sock_log("accept err=%d", errno);
			break;
		}
		sock_log("accepted fd=%d", fd);

		/* Set non-blocking so writes from input.c don't stall */
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags >= 0)
			fcntl(fd, F_SETFL, flags | O_NONBLOCK);

		/* Replace previous client fd atomically */
		int old = active_client_fd;
		active_client_fd = fd;
		if (old >= 0)
			close(old);

		/* Don't read from fd here — main thread will drain it via
		 * proce_event()/socket reads if needed. We just keep
		 * accepting new connections. */
	}
	return NULL;
}

void socket_setup() {
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);

	int flags = fcntl(sock, F_GETFL, 0);
	if (flags >= 0)
		fcntl(sock, F_SETFL, flags | O_NONBLOCK);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, "/tmp/vcomp.sock");

	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		sock_log("bind failed");
		return;
	}
	sock_log("bind ok");

	listen(sock, 8);
	sock_log("listen backlog=8");

	/* spawn accept thread */
	pthread_t tid;
	pthread_create(&tid, NULL, socket_accept_loop, (void*)(intptr_t)sock);
	pthread_detach(tid);
}

static int fds[2] = {-1, -1};
static const char* dev_types[2] = {"unknown", "unknown"};
static size_t device_count = 0;

void input_setup() {
	char* devices[] = {"/dev/input/event0", "/dev/input/event1"};
	device_count = sizeof(devices) / sizeof(devices[0]);

	for (size_t i = 0; i < device_count; i++) {
		fds[i] = open_evdev(devices[i]);
		if (fds[i] >= 0) {
			dev_types[i] = evdev_detect_device(devices[i]);
			query_abs_range(fds[i], i);
		}
	}
}

/* cursor & drag state */
int need_rerender = true;
float g_screen_w = 1920.0f;
float g_screen_h = 1080.0f;

/* window drag state */
bool window_is_dragging = false;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static window_scene_t* g_window = NULL;
static worker_args_t* g_args = NULL;

void check_titlebar_click(int px, int py) {
	if (!g_window || !g_args)
		return;

	bool in_titlebar = px >= g_window->x && px <= g_window->x + g_window->w && py >= g_window->y && py <= g_window->y + g_window->titlebar_height;

	if (in_titlebar) {
		window_is_dragging = true;
		drag_offset_x = px - g_window->x;
		drag_offset_y = py - g_window->y;
		need_rerender = true;
	}
}

static void render_debug_overlay(worker_args_t* args, window_scene_t* window);

#define OVERLAY_X 0
#define OVERLAY_Y 0
#define OVERLAY_W 520
#define OVERLAY_H 500

int main(void) {
	// debug_event();
	input_setup();
	socket_setup();
	vxair_device_t* dev = vxair_device_create(VXAIR_BACKEND_VIRGL);

	worker_args_t* args = calloc(1, sizeof(worker_args_t));

	args->screen_w = 1366.0f;
	args->screen_h = 768.0f;

	g_screen_w = args->screen_w;
	g_screen_h = args->screen_h;

	/* sync cursor pos dengan center screen (match kernel cursor init) */
	pos_x = (int)(args->screen_w / 2);
	pos_y = (int)(args->screen_h / 2);

	args->dev = dev;
	args->ctx = vxair_context_create(args->dev, args->screen_w, args->screen_h);

	vxair_switch_context(args->dev, args->ctx);
	vxair_set_scanout(args->dev, args->ctx, 0);

	pthread_t loader_tid;
	pthread_create(&loader_tid, NULL, loader_worker, args);

	splash_scene_t* splash = splash_init(dev, args->ctx, args);
	wallpaper_scene_t* wallpaper = NULL;

	// sample window
	window_scene_t* window = window_init(dev, args->ctx, args);
	g_window = window;
	g_args = args;

	// uint64_t last = time_ns();

	const uint64_t target_frame_ns = 1000000000ULL / 60;

	window->w = 500;
	window->h = 500;
	window->use_texture = false;

	while (1) {
		uint64_t frame_start = time_ns();

		/* Poll input events */
		for (size_t i = 0; i < 2; i++) {
			if (fds[i] >= 0) {
				proces_event(fds[i], i);
			}
		}

		update_event_rate(frame_start);

		/* Drain inbound messages from vxterm (SUBMIT_RESOURCE, LOG...) */
		if (active_client_fd >= 0) {
			char ibuf[512];
			ssize_t n = read(active_client_fd, ibuf, sizeof(ibuf) - 1);
			while (n > 0) {
				size_t off = 0;
				while (off + sizeof(struct message) <= (size_t)n) {
					struct message* msg = (struct message*)(ibuf + off);
					size_t msz = sizeof(struct message) + msg->len;
					if (off + msz > (size_t)n)
						break;
					switch (msg->type) {
					case VCOMP_SUBMIT_RESOURCE:
						client_id = msg->data[0];
						sock_log("resource id: %d", client_id);
						break;
					case VCOMP_LOG:
						sock_log("[vxterm] %s", (char*)msg->data);
						break;
					default:
						break;
					}
					off += msz;
				}
				n = read(active_client_fd, ibuf, sizeof(ibuf) - 1);
			}
		}

		if (window_is_dragging) {
			window->x = pos_x - drag_offset_x;
			window->y = pos_y - drag_offset_y;

			if (window->x < 0.0f)
				window->x = 0.0f;
			if (window->y < 0.0f)
				window->y = 0.0f;

			if (window->y + window->h / 2 > args->screen_h)
				window->y = args->screen_h - window->h / 2;

			need_rerender = true;
		}

		if (!args->early_phase_done) {
			splash_render(splash);
			vxair_submit_and_present(splash->ctx);
		} else {

			if (!wallpaper) {
				wallpaper = wallpaper_init(dev, args->ctx, args);
				splash_destroy(splash);
				splash = NULL;
			}

			if (need_rerender) {
				wallpaper_render(wallpaper, args, true);
			} else {
				vxair_cmd_set_scissor(args->ctx, OVERLAY_X, OVERLAY_Y, OVERLAY_W, OVERLAY_H);
				wallpaper_render(wallpaper, args, false);
				vxair_cmd_disable_scissor(args->ctx);
			}

			render_debug_overlay(args, window);

			{

				int32_t scx = (uint32_t)window->x - 30;
				int32_t scy = (uint32_t)window->y - 30;
				int32_t scw = (uint32_t)window->w + 60;
				int32_t sch = (uint32_t)window->h + 60;

				vxair_cmd_set_scissor(args->ctx, scx > 0 ? scx : 0, scy > 0 ? scy : 0, scw > g_screen_w ? g_screen_w : scw,
				                      sch > g_screen_h ? g_screen_h : sch);
			}

			wallpaper_render(wallpaper, args, false);
			render_debug_overlay(args, window);
			window_render(window, args);
			vxair_cmd_disable_scissor(args->ctx);

			for (size_t i = 0; i < 2; i++) {
				if (fds[i] >= 0) {
					proces_event(fds[i], i);
				}
			}

			if (client_id != 0) {
				static uint32_t imported_id = 0;
				if (imported_id != client_id) {
					if (window->fb_tex) {
						// Optionally destroy the old
						// texture struct
					}
					window->fb_tex = vxair_texture_import(dev, client_id, 500, 470, VXAIR_FORMAT_RGBA8);
					imported_id = client_id;
					window->use_texture = 1;
				}
				vxair_cmd_bind_texture(args->ctx, 0, window->fb_tex);

				// vxair_texture_attach(args->dev,
				// window->fb_tex, 0);
				window->use_texture = 1;
			}

			vxair_submit_and_present(args->ctx);
			need_rerender = false;
		}

		if (right_button_just_pressed && !prev_right_button) {
			right_button_just_pressed = false;
			usleep(100);
			need_rerender = true;
		}

		if (left_button_just_pressed && !prev_left_button) {
			left_button_just_pressed = false;
			usleep(100);
			need_rerender = true;
		}

		uint64_t elapsed = time_ns() - frame_start;

		if (elapsed < target_frame_ns && args->early_phase_done) {
			sleep_ns(target_frame_ns - elapsed);
		}
	}

	splash_destroy(splash);
	pthread_join(loader_tid, NULL);
	vxui_text_renderer_destroy(args->dev, args->text_renderer);
	vxui_font_destroy(args->dev, args->font);
	free(args);
	return 0;
}

static void render_debug_overlay(worker_args_t* args, window_scene_t* window) {
	if (!args->font || !args->text_renderer || !window)
		return;

	char debug_text[1024];
	debug_text[0] = '\0';
	int off = 0;

	/* append socket debug lines */
	if (sock_dbg_idx > 0) {
		int start = sock_dbg_idx > SOCK_DBG_LINES ? (sock_dbg_idx - SOCK_DBG_LINES) % SOCK_DBG_LINES : 0;
		int count = sock_dbg_idx < SOCK_DBG_LINES ? sock_dbg_idx : SOCK_DBG_LINES;
		for (int i = 0; i < count && off < (int)sizeof(debug_text) - 80; i++) {
			off += snprintf(debug_text + off, sizeof(debug_text) - (size_t)off, "log: %s\n", sock_dbg[(start + i) % SOCK_DBG_LINES]);
		}
	}

	vxair_cmd_set_viewport(args->ctx, 0.0f, 0.0f, args->screen_w, args->screen_h, 0.0f, 0.0f);
	vxair_bind_blend(args->ctx, window->alpha_blend_id);

	vxui_text_desc_t text_opts = {
	    .scale = 1.0f,
	    .color = 0xFFFFFFFF,
	    .align = VXUI_ALIGN_LEFT,
	    .bg_color = 0x000000A0,
	};
	static vxair_buffer_t* text_vbo = NULL;
	vxui_draw_text(args->text_renderer, args->dev, args->ctx, args->font, debug_text, 20.0f, 20.0f, &text_opts, &text_vbo);
}
