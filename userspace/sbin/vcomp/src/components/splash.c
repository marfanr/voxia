#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <vcomp.h>
#include <vxair.h>
#include <unistd.h>

static const char* SPLASH_VS =
    "VERT\n"
    "DCL IN[0]\n"
    "DCL IN[1]\n"
    "DCL OUT[0], POSITION\n"
    "DCL OUT[1], GENERIC[0]\n"
    "DCL CONST[0..3]\n"
    "DP4 OUT[0].x, IN[0], CONST[0]\n"
    "DP4 OUT[0].y, IN[0], CONST[1]\n"
    "DP4 OUT[0].z, IN[0], CONST[2]\n"
    "DP4 OUT[0].w, IN[0], CONST[3]\n"
    "MOV OUT[1], IN[1]\n"
    "END\n";

static const char* SPLASH_FS =
    "FRAG\n"
    "DCL IN[0], GENERIC[0], PERSPECTIVE\n"
    "DCL OUT[0], COLOR\n"
    "DCL SAMP[0]\n"
    "DCL SVIEW[0], 2D, FLOAT\n"
    "TEX OUT[0], IN[0], SAMP[0], 2D\n"
    "END\n";

splash_scene_t* splash_init(vxair_device_t* dev,  vxair_context_t* ctx, worker_args_t* args) {
	splash_scene_t* s = calloc(1, sizeof(splash_scene_t));

	s->ctx = ctx;
	s->vs  = vxair_shader_create(dev, VXAIR_SHADER_VERTEX,   SPLASH_VS);
	s->fs  = vxair_shader_create(dev, VXAIR_SHADER_FRAGMENT, SPLASH_FS);

	uint32_t img_w, img_h;
	uint32_t* logo_px = load_bmp32_raw("/usr/shared/boot/splash.bmp", &img_w, &img_h);
	if (!logo_px) {
		printf("[SPLASH] Gagal load /usr/shared/boot/splash.bmp!\n");
	}

	float w = 200.0f;
	float h = (200.0f * (float)img_h) / (float)img_w;
	float x = (args->screen_w / 2.0f) - (w / 2.0f);
	float y = (args->screen_h  / 2.0f) - (h / 2.0f) - 30.0f;

	float logo_verts[] = {
	    x,     y,     0.0f, 0.0f,
	    x,     y + h, 0.0f, 1.0f,
	    x + w, y + h, 1.0f, 1.0f,
	    x,     y,     0.0f, 0.0f,
	    x + w, y + h, 1.0f, 1.0f,
	    x + w, y,     1.0f, 0.0f,
	};
	s->vbo_logo  = vxair_buffer_create(dev, sizeof(logo_verts), logo_verts);
	if (logo_px) {
		s->tex_logo  = vxair_texture_create(dev, img_w, img_h, VXAIR_FORMAT_RGBA8, logo_px);
		free(logo_px);
	}

	/* Loading spinner */
	uint32_t load_w, load_h;
	uint32_t* load_px = load_bmp32_raw("/usr/shared/boot/loading.bmp", &load_w, &load_h);
	if (!load_px) {
		printf("[SPLASH] Gagal load /usr/shared/boot/loading.bmp!\n");
	}

	float lw = 20.0f;
	float lh = (20.0f * (float)load_h) / (float)load_w;
	float hw = lw / 2.0f, hh = lh / 2.0f;
	float load_verts[] = {
	    -hw, -hh, 0.0f, 0.0f,
	    -hw,  hh, 0.0f, 1.0f,
	     hw,  hh, 1.0f, 1.0f,
	    -hw, -hh, 0.0f, 0.0f,
	     hw,  hh, 1.0f, 1.0f,
	     hw, -hh, 1.0f, 0.0f,
	};
	s->vbo_loading  = vxair_buffer_create(dev, sizeof(load_verts), load_verts);
	if (load_px) {
		s->tex_loading  = vxair_texture_create(dev, load_w, load_h, VXAIR_FORMAT_RGBA8, load_px);
		free(load_px);
	}

	/* Precompute MVP ortho untuk logo */
	s->Sx = 2.0f / args->screen_w;
	s->Sy = 2.0f / args->screen_h;
	s->Tx = -1.0f;
	s->Ty = -1.0f;
	float* m = s->mvp_logo;
	m[0]=s->Sx; m[1]=0;    m[2]=0;    m[3]=s->Tx;
	m[4]=0;     m[5]=s->Sy;m[6]=0;    m[7]=s->Ty;
	m[8]=0;     m[9]=0;    m[10]=1;   m[11]=0;
	m[12]=0;    m[13]=0;   m[14]=0;   m[15]=1;

	s->load_cx = args->screen_w / 2.0f;
	s->load_cy = y + h + 120.0f + hh;

	/* Scissor box (top-left corner + size) untuk spinner region */
	s->scissor_x = (int)(s->load_cx - hw - 2);
	s->scissor_y = (int)(s->load_cy - hh - 2);
	s->scissor_w = (int)(lw + 4);
	s->scissor_h = (int)(lh + 4);

	s->bg_color = rgba_from_hex(0);
	s->angle    = 0.0f;

	/* Setup tetap di context sekali sebelum loop */
	vxair_cmd_set_viewport(s->ctx, 0.0f, 0.0f, args->screen_w, args->screen_h, 0.0f, 0.0f);
	vxair_cmd_bind_shader(s->ctx, s->vs);
	vxair_cmd_bind_shader(s->ctx, s->fs);
	vxair_vertex_element_t elems[2] = {
	    {0, VXAIR_VERTEX_FORMAT_FLOAT2},
	    {8, VXAIR_VERTEX_FORMAT_FLOAT2},
	};
	vxair_cmd_bind_vertex_elements(s->ctx, 2, elems);

	return s;
}

static int logo_already_rendered = 0;

void splash_render(splash_scene_t* s) {
	rgba8_t* bg = &s->bg_color;
	vxair_cmd_clear(s->ctx,
	                bg->r / 255.0f, bg->g / 255.0f,
	                bg->b / 255.0f, bg->a / 255.0f);

	/* Logo */
	// if (logo_already_rendered) {
	// 	vxair_cmd_set_scissor(s->ctx, s->scissor_x, s->scissor_y,
	// 	                      s->scissor_w, s->scissor_h);
	// }
	
	if (s->tex_logo) {
		vxair_cmd_set_constant_buffer(s->ctx, VXAIR_SHADER_VERTEX, 0,
		                              sizeof(s->mvp_logo), s->mvp_logo);
		vxair_cmd_bind_texture(s->ctx, 0, s->tex_logo);
		vxair_cmd_bind_vertex_buffer(s->ctx, s->vbo_logo, 4 * sizeof(float));
		vxair_cmd_draw_arrays(s->ctx, VXAIR_PRIMITIVE_TRIANGLES, 0, 6);
		logo_already_rendered = 1;
	}

	/* Loading spinner */
	if (s->tex_loading) {
		float c = cosf(s->angle), ss = sinf(s->angle);


		float mvp[16] = {
		    s->Sx * c,  s->Sx * -ss, 0.0f, s->Sx * s->load_cx + s->Tx,
		    s->Sy * ss, s->Sy *  c,  0.0f, s->Sy * s->load_cy + s->Ty,
		    0.0f,       0.0f,        1.0f, 0.0f,
		    0.0f,       0.0f,        0.0f, 1.0f,
		};
		vxair_cmd_set_constant_buffer(s->ctx, VXAIR_SHADER_VERTEX, 0,
		                              sizeof(mvp), mvp);
		vxair_cmd_bind_texture(s->ctx, 0, s->tex_loading);
		vxair_cmd_bind_vertex_buffer(s->ctx, s->vbo_loading, 4 * sizeof(float));
		vxair_cmd_draw_arrays(s->ctx, VXAIR_PRIMITIVE_TRIANGLES, 0, 6);
		
		// vxair_cmd_disable_scissor(s->ctx);
	}

	s->angle -= 0.05f;
	if (s->angle < -6.2831853f)
		s->angle += 6.2831853f;

	usleep(200);
}

void splash_destroy(splash_scene_t* s) {
	if (!s) return;
	free(s);
}