#include <stdlib.h>
#include <vcomp.h>
#include <vxair.h>

static const char* WALL_VS =
    "VERT\n"
    "DCL IN[0]\n"
    "DCL IN[1]\n"
    "DCL OUT[0], POSITION\n"
    "DCL OUT[1], GENERIC[0]\n"
    "DCL OUT[2], GENERIC[1]\n"
    "DCL CONST[0..2]\n"
    "DCL TEMP[0]\n"

    // Scale normalized (0,1) coords dengan w,h dari CONST[2].zw
    "MUL TEMP[0].x, IN[0].xxxx, CONST[2].zzzz\n" // * w
    "MUL TEMP[0].y, IN[0].yyyy, CONST[2].wwww\n" // * h

    // Add window position offset (s->x, s->y dari CONST[2].xy)
    "ADD TEMP[0].x, TEMP[0].xxxx, CONST[2].xxxx\n" // + x
    "ADD TEMP[0].y, TEMP[0].yyyy, CONST[2].yyyy\n" // + y

    // Simpan pixel coords untuk fragment shader (SDF calculations)
    "MOV OUT[2], TEMP[0]\n"

    // Transform ke NDC (NDC_x = pixel_x * scale_x + bias_x)
    "MUL TEMP[0].x, TEMP[0].xxxx, CONST[0].xxxx\n"
    "ADD TEMP[0].x, TEMP[0].xxxx, CONST[0].zzzz\n"

    "MUL TEMP[0].y, TEMP[0].yyyy, CONST[0].yyyy\n"
    "ADD TEMP[0].y, TEMP[0].yyyy, CONST[0].wwww\n"

    "MOV TEMP[0].z, CONST[1].xxxx\n"
    "MOV TEMP[0].w, CONST[1].yyyy\n"

    "MOV OUT[0], TEMP[0]\n"
    "MOV OUT[1], IN[1]\n" // warna

    "END\n";

static const char* WALL_FS =
    "FRAG\n"
    "DCL IN[0], GENERIC[0], PERSPECTIVE\n"
    "DCL IN[1], GENERIC[1], PERSPECTIVE\n"
    "DCL OUT[0], COLOR\n"
    "DCL CONST[0..9]\n"
    "DCL SAMP[0]\n"

    "DCL TEMP[0]\n"
    "DCL TEMP[1]\n"
    "DCL TEMP[2]\n"
    "DCL TEMP[3]\n"
    "DCL TEMP[4]\n"
    "DCL TEMP[5]\n"
    "DCL TEMP[6]\n"
    "DCL TEMP[7]\n"
    "DCL TEMP[8]\n"

    // Rounded Outer
    "SUB TEMP[0].x, IN[1].xxxx, CONST[0].xxxx\n"
    "SUB TEMP[0].y, IN[1].yyyy, CONST[0].yyyy\n"

    "ABS TEMP[1].x, TEMP[0].xxxx\n"
    "ABS TEMP[1].y, TEMP[0].yyyy\n"
    "SUB TEMP[1].x, TEMP[1].xxxx, CONST[1].xxxx\n"
    "SUB TEMP[1].y, TEMP[1].yyyy, CONST[1].yyyy\n"

    "MAX TEMP[2].x, TEMP[1].xxxx, CONST[1].zzzz\n"
    "MAX TEMP[2].y, TEMP[1].yyyy, CONST[1].zzzz\n"

    "MUL TEMP[3].x, TEMP[2].xxxx, TEMP[2].xxxx\n"
    "MUL TEMP[3].y, TEMP[2].yyyy, TEMP[2].yyyy\n"
    "ADD TEMP[3].x, TEMP[3].xxxx, TEMP[3].yyyy\n"
    "SQRT TEMP[3].x, TEMP[3].xxxx\n"

    "MAX TEMP[3].y, TEMP[1].xxxx, TEMP[1].yyyy\n"
    "MIN TEMP[3].y, TEMP[3].yyyy, CONST[1].zzzz\n"

    "ADD TEMP[3].x, TEMP[3].xxxx, TEMP[3].yyyy\n"
    "SUB TEMP[3].x, TEMP[3].xxxx, CONST[1].wwww\n"

    "MAD_SAT TEMP[4].x, TEMP[3].xxxx, CONST[0].zzzz, CONST[0].wwww\n"
    "MUL TEMP[4].y, TEMP[4].xxxx, IN[0].wwww\n"

    // BLEND TITLE BAR
    "SUB TEMP[5].x, IN[1].yyyy, CONST[2].wwww\n"
    "MAD_SAT TEMP[5].x, TEMP[5].xxxx, CONST[0].zzzz, CONST[0].wwww\n"

    "SUB TEMP[1].x, CONST[2].xxxx, IN[0].xxxx\n"
    "SUB TEMP[1].y, CONST[2].yyyy, IN[0].yyyy\n"
    "SUB TEMP[1].z, CONST[2].zzzz, IN[0].zzzz\n"

    "MAD TEMP[6].x, TEMP[5].xxxx, TEMP[1].xxxx, IN[0].xxxx\n"
    "MAD TEMP[6].y, TEMP[5].xxxx, TEMP[1].yyyy, IN[0].yyyy\n"
    "MAD TEMP[6].z, TEMP[5].xxxx, TEMP[1].zzzz, IN[0].zzzz\n"

    // INNER AREA (concentric rounded box SDF)
    "MOV TEMP[7], TEMP[6]\n"

    "SUB TEMP[5].x, IN[1].xxxx, CONST[8].xxxx\n"
    "SUB TEMP[5].y, IN[1].yyyy, CONST[8].yyyy\n"

    "ABS TEMP[5].x, TEMP[5].xxxx\n"
    "ABS TEMP[5].y, TEMP[5].yyyy\n"
    "SUB TEMP[5].x, TEMP[5].xxxx, CONST[8].zzzz\n"
    "SUB TEMP[5].y, TEMP[5].yyyy, CONST[8].wwww\n"

    "MAX TEMP[2].x, TEMP[5].xxxx, CONST[1].zzzz\n"
    "MAX TEMP[2].y, TEMP[5].yyyy, CONST[1].zzzz\n"

    "MUL TEMP[3].x, TEMP[2].xxxx, TEMP[2].xxxx\n"
    "MUL TEMP[3].y, TEMP[2].yyyy, TEMP[2].yyyy\n"
    "ADD TEMP[3].x, TEMP[3].xxxx, TEMP[3].yyyy\n"
    "SQRT TEMP[3].x, TEMP[3].xxxx\n"

    "MAX TEMP[3].y, TEMP[5].xxxx, TEMP[5].yyyy\n"
    "MIN TEMP[3].y, TEMP[3].yyyy, CONST[1].zzzz\n"

    "ADD TEMP[3].x, TEMP[3].xxxx, TEMP[3].yyyy\n"
    "SUB TEMP[3].x, TEMP[3].xxxx, CONST[9].xxxx\n"

    "MAD_SAT TEMP[5].w, TEMP[3].xxxx, CONST[0].zzzz, CONST[0].wwww\n"

    "MUL TEMP[5].w, TEMP[5].wwww, TEMP[4].xxxx\n"

    "MOV TEMP[7].w, TEMP[5].wwww\n"

    "SUB TEMP[1].x, CONST[3].xxxx, TEMP[7].xxxx\n"
    "SUB TEMP[1].y, CONST[3].yyyy, TEMP[7].yyyy\n"
    "SUB TEMP[1].z, CONST[3].zzzz, TEMP[7].zzzz\n"
    "MAD TEMP[6].x, TEMP[5].wwww, TEMP[1].xxxx, TEMP[7].xxxx\n"
    "MAD TEMP[6].y, TEMP[5].wwww, TEMP[1].yyyy, TEMP[7].yyyy\n"
    "MAD TEMP[6].z, TEMP[5].wwww, TEMP[1].zzzz, TEMP[7].zzzz\n"

    // BORDER BOTTOM
    "SUB TEMP[5].x, IN[1].yyyy, CONST[2].wwww\n"
    "ABS TEMP[5].x, TEMP[5].xxxx\n"
    "ADD TEMP[5].y, CONST[0].wwww, CONST[0].wwww\n"
    "MAD_SAT TEMP[5].x, TEMP[5].xxxx, CONST[4].wwww, TEMP[5].yyyy\n"

    "SUB TEMP[2].x, CONST[4].xxxx, TEMP[6].xxxx\n"
    "SUB TEMP[2].y, CONST[4].yyyy, TEMP[6].yyyy\n"
    "SUB TEMP[2].z, CONST[4].zzzz, TEMP[6].zzzz\n"

    "MAD OUT[0].x, TEMP[5].xxxx, TEMP[2].xxxx, TEMP[6].xxxx\n"
    "MAD OUT[0].y, TEMP[5].xxxx, TEMP[2].yyyy, TEMP[6].yyyy\n"
    "MAD OUT[0].z, TEMP[5].xxxx, TEMP[2].zzzz, TEMP[6].zzzz\n"

    // TEXTURE
    // TEXTURE
    "SUB TEMP[0].x, CONST[8].xxxx, CONST[7].xxxx\n"
    "SUB TEMP[0].x, IN[1].xxxx, TEMP[0].xxxx\n"
    "MUL TEMP[0].x, TEMP[0].xxxx, CONST[7].zzzz\n"

    "SUB TEMP[0].y, CONST[8].yyyy, CONST[7].yyyy\n"
    "SUB TEMP[0].y, IN[1].yyyy, TEMP[0].yyyy\n"
    "MUL TEMP[0].y, TEMP[0].yyyy, CONST[7].wwww\n"

    "TEX TEMP[8], TEMP[0], SAMP[0], 2D\n"
    "MUL TEMP[5].y, CONST[5].xxxx, TEMP[7].wwww\n"

    "SUB TEMP[2].x, TEMP[8].xxxx, OUT[0].xxxx\n"
    "SUB TEMP[2].y, TEMP[8].yyyy, OUT[0].yyyy\n"
    "SUB TEMP[2].z, TEMP[8].zzzz, OUT[0].zzzz\n"

    "MAD OUT[0].x, TEMP[5].yyyy, TEMP[2].xxxx, OUT[0].xxxx\n"
    "MAD OUT[0].y, TEMP[5].yyyy, TEMP[2].yyyy, OUT[0].yyyy\n"
    "MAD OUT[0].z, TEMP[5].yyyy, TEMP[2].zzzz, OUT[0].zzzz\n"

    // // inactive
    // // luminance
    "ADD TEMP[1].x, OUT[0].xxxx, OUT[0].yyyy\n"
    "ADD TEMP[1].x, TEMP[1].xxxx, OUT[0].zzzz\n"
    "MUL TEMP[1].x, TEMP[1].xxxx, CONST[5].zzzz\n" // rata2 grayscale

    // // lerp antara color asli dan grayscale berdasarkan dim
    "SUB TEMP[2].x, TEMP[1].xxxx, OUT[0].xxxx\n"
    "SUB TEMP[2].y, TEMP[1].xxxx, OUT[0].yyyy\n"
    "SUB TEMP[2].z, TEMP[1].xxxx, OUT[0].zzzz\n"

    // // 1 - dim_factor = seberapa banyak desaturasi
    "SUB TEMP[3].x, CONST[5].wwww, CONST[5].yyyy\n"

    "MAD OUT[0].x, TEMP[2].xxxx, TEMP[3].xxxx, OUT[0].xxxx\n"
    "MAD OUT[0].y, TEMP[2].yyyy, TEMP[3].xxxx, OUT[0].yyyy\n"
    "MAD OUT[0].z, TEMP[2].zzzz, TEMP[3].xxxx, OUT[0].zzzz\n"

    "MUL OUT[0].x, OUT[0].xxxx, CONST[5].yyyy\n"
    "MUL OUT[0].y, OUT[0].yyyy, CONST[5].yyyy\n"
    "MUL OUT[0].z, OUT[0].zzzz, CONST[5].yyyy\n"

    "MOV OUT[0].w, TEMP[4].yyyy\n"

    "END\n";

// Font shaders are now handled by vxui

//  Shadow
static const char* SHADOW_VS =
    "VERT\n"
    "DCL IN[0]\n"
    "DCL OUT[0], POSITION\n"
    "DCL OUT[1], GENERIC[0]\n"
    "DCL OUT[2], GENERIC[1]\n" // [FIX 1] Wajib dideklarasikan! Jika tidak, TGSI
                               // parser akan reject karena OUT[2] dipakai di
                               // bawah.
    "DCL CONST[0..2]\n"
    "DCL TEMP[0]\n"

    // Scale normalized (0,1) coords dengan w,h dari CONST[2].zw
    "MUL TEMP[0].x, IN[0].xxxx, CONST[2].zzzz\n"
    "MUL TEMP[0].y, IN[0].yyyy, CONST[2].wwww\n"

    // Add window position offset (s->x, s->y dari CONST[2].xy)
    "ADD TEMP[0].x, TEMP[0].xxxx, CONST[2].xxxx\n"
    "ADD TEMP[0].y, TEMP[0].yyyy, CONST[2].yyyy\n"

    // Simpan pixel coords untuk fragment shader (SDF calculations)
    "MOV OUT[2], TEMP[0]\n"

    // [FIX 2] Kirim juga UV standar (0..1) ke OUT[1] agar tidak berisi nilai
    // undefined di Fragment Shader
    "MOV OUT[1], IN[0]\n"

    // Transform ke NDC (NDC_x = pixel_x * scale_x + bias_x)
    "MUL TEMP[0].x, TEMP[0].xxxx, CONST[0].xxxx\n"
    "ADD TEMP[0].x, TEMP[0].xxxx, CONST[0].zzzz\n"

    "MUL TEMP[0].y, TEMP[0].yyyy, CONST[0].yyyy\n"
    "ADD TEMP[0].y, TEMP[0].yyyy, CONST[0].wwww\n"

    "MOV TEMP[0].z, CONST[1].xxxx\n"
    "MOV TEMP[0].w, CONST[1].yyyy\n"

    "MOV OUT[0], TEMP[0]\n"
    "END\n";

static const char* SHADOW_FS =
    "FRAG\n"
    "DCL IN[0], GENERIC[0], PERSPECTIVE\n"
    "DCL IN[1], GENERIC[1], PERSPECTIVE\n"
    "DCL OUT[0], COLOR\n"
    "DCL CONST[0..1]\n" // CONST[0] = { Center_X, Center_Y, Half_W, Half_H }
                        // CONST[1] = { Shadow_Radius, Max_Alpha,
                        // Window_Radius, 1.0 }
    "DCL TEMP[0..1]\n"
    "IMM FLT32 { 0.0, 1.0, 0.0, 0.0 }\n" // IMM[0].x = 0.0, IMM[0].y = 1.0

    /* BORDER RADIUS */
    // ABS (pos  - center)
    "SUB TEMP[0].xy, IN[1].xyyy, CONST[0].xyyy\n"
    "ABS TEMP[0].xy, TEMP[0].xyyy\n"

    //  - (setengah_ukuran - radius_jendela)
    "SUB TEMP[1].xy, CONST[0].zwww, CONST[1].zzzz\n"
    "SUB TEMP[0].xy, TEMP[0].xyyy, TEMP[1].xyyy\n"

    // 3. Kunci nilai negatif ke 0.0
    "MAX TEMP[0].xy, TEMP[0].xyyy, IMM[0].xxxx\n"

    // 4. Hitung panjang vektor sudut (Euclidean length): d = sqrt(dx^2 + dy^2)
    "DP2 TEMP[0].x, TEMP[0], TEMP[0]\n"
    "SQRT TEMP[0].x, TEMP[0].xxxx\n"

    // 5. Kurangi dengan radius jendela asli -> d_outer = length(d) -
    // radius_jendela
    "SUB TEMP[0].x, TEMP[0].xxxx, CONST[1].zzzz\n"

    // 6. Kita hanya peduli jarak di luar batas rounded window (MAX d_outer,
    // 0.0)
    "MAX TEMP[0].x, TEMP[0].xxxx, IMM[0].xxxx\n"

    // rasio = d_outer / shadow_radius
    "DIV TEMP[0].x, TEMP[0].xxxx, CONST[1].xxxx\n"

    // t = 1.0 - rasio
    "SUB TEMP[0].x, IMM[0].yyyy, TEMP[0].xxxx\n"

    // Clamped
    "MAX TEMP[0].x, TEMP[0].xxxx, IMM[0].xxxx\n"
    "MIN TEMP[0].x, TEMP[0].xxxx, IMM[0].yyyy\n"

    // t^3
    "MOV TEMP[1].x, TEMP[0].xxxx\n"
    "MUL TEMP[0].x, TEMP[0].xxxx, TEMP[0].xxxx\n" // t^2
    "MUL TEMP[0].x, TEMP[0].xxxx, TEMP[1].xxxx\n" // t^3 (Super smooth
                                                  // fade-out!)

    // 11. Kalikan dengan Opacity Maksimal Shadow (CONST[1].y)
    "MUL TEMP[0].x, TEMP[0].xxxx, CONST[1].yyyy\n"

    // 12. Output Warna Hitam RGB(0,0,0) + Alpha hasil kalkulasi
    "MOV OUT[0].xyz, IMM[0].xxxx\n"
    "MOV OUT[0].w, TEMP[0].xxxx\n"
    "END\n";

window_scene_t* window_init(vxair_device_t* dev, vxair_context_t* ctx,
                            worker_args_t* args) {
	(void)args;

	window_scene_t* s = calloc(1, sizeof(window_scene_t));
	{
		s->ctx = ctx;
		s->vs = vxair_shader_create(dev, VXAIR_SHADER_VERTEX, WALL_VS);
		s->fs =
		    vxair_shader_create(dev, VXAIR_SHADER_FRAGMENT, WALL_FS);
		s->titlebar_height = 30.0f;
		s->w = 600.0f;
		s->h = 400.0f + s->titlebar_height;
		s->x = args->screen_w / 2 - 300.0f;
		s->y = 100;
		s->shadow_color = rgba_from_hex(0x000000FF);
		s->titlebar_height = 30.0f;

		s->alpha_blend_id = vxair_create_alpha_blend(ctx);

		rgba8_t bg = rgba_from_hex(0xFFFFFFFF);

		// Normalized quad (0,0) to (1,1) - di-scale di vertex shader
		// menggunakan CONST[2].zw (w, h)
		float verts[] = {
		    0,
		    0,
		    bg.r / 255.0f,
		    bg.g / 255.0f,
		    bg.b / 255.0f,
		    bg.a / 255.0f,
		    0,
		    1,
		    bg.r / 255.0f,
		    bg.g / 255.0f,
		    bg.b / 255.0f,
		    bg.a / 255.0f,
		    1,
		    1,
		    bg.r / 255.0f,
		    bg.g / 255.0f,
		    bg.b / 255.0f,
		    bg.a / 255.0f,
		    0,
		    0,
		    bg.r / 255.0f,
		    bg.g / 255.0f,
		    bg.b / 255.0f,
		    bg.a / 255.0f,
		    1,
		    1,
		    bg.r / 255.0f,
		    bg.g / 255.0f,
		    bg.b / 255.0f,
		    bg.a / 255.0f,
		    1,
		    0,
		    bg.r / 255.0f,
		    bg.g / 255.0f,
		    bg.b / 255.0f,
		    bg.a / 255.0f,
		};

		s->vbo = vxair_buffer_create(dev, sizeof(verts), verts);

		s->fb_tex = 0;
		s->use_texture = 1;
	}

	// shadow
	{
		float shadow_vertices[] = {
		    0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0,
		};
		s->shadow_vbo = vxair_buffer_create(
		    dev, sizeof(shadow_vertices), shadow_vertices);

		s->shadow_vs =
		    vxair_shader_create(dev, VXAIR_SHADER_VERTEX, SHADOW_VS);
		s->shadow_fs =
		    vxair_shader_create(dev, VXAIR_SHADER_FRAGMENT, SHADOW_FS);
	}

	return s;
}

void window_shadow_render(window_scene_t* s, worker_args_t* args) {
	(void)args;

	// Tentukan spesifikasi bayangan
	const float padding = 30.0f;
	const float max_alpha = 0.3f;
	const float win_radius = 8.0f;

	vxair_cmd_bind_shader(s->ctx, s->shadow_vs);
	vxair_cmd_bind_shader(s->ctx, s->shadow_fs);

	// Hitung dimensi quad yang sudah diperbesar (Vertex Padding)
	float shadow_x = s->x - padding;
	float shadow_y = s->y - padding;
	float shadow_w = s->w + (padding * 2.0f);
	float shadow_h = s->h + (padding * 2.0f);

	float uniform[] = {
	    2.0f / args->screen_w,
	    2.0f / args->screen_h,
	    -1.0f,
	    -1.0f, /* CONST 0: NDC scale/bias */
	    0.0f,
	    1.0f,
	    0.0f,
	    0.0f, /* CONST 1: z, w */
	    shadow_x,
	    shadow_y,
	    shadow_w,
	    shadow_h,
	};
	vxair_cmd_set_constant_buffer(s->ctx, VXAIR_SHADER_VERTEX, 0,
	                              sizeof(uniform), uniform);

	// Hitung titik tengah dan setengah ukuran dari JENDELA ASLI
	float win_center_x = s->x + (s->w * 0.5f);
	float win_center_y = s->y + (s->h * 0.5f);
	float win_half_w = s->w * 0.5f;
	float win_half_h = s->h * 0.5f;

	float shadow_uniform[] = {
	    win_center_x, win_center_y,
	    win_half_w,   win_half_h, /* CONST 0: Specs Jendela Asli */
	    padding,      max_alpha,
	    win_radius,   1.0f /* CONST 1: Specs Bayangan */
	};
	vxair_cmd_set_constant_buffer(s->ctx, VXAIR_SHADER_FRAGMENT, 0,
	                              sizeof(shadow_uniform), shadow_uniform);

	vxair_vertex_element_t elems[] = {
	    {0, VXAIR_VERTEX_FORMAT_FLOAT2},
	};
	vxair_cmd_bind_vertex_elements(s->ctx, 1, elems);
	vxair_cmd_bind_vertex_buffer(s->ctx, s->shadow_vbo, 2 * sizeof(float));

	// Pastikan Alpha Blending di-enable sebelum draw call ini jika belum
	// aktif di pipeline!
	vxair_cmd_draw_arrays(s->ctx, VXAIR_PRIMITIVE_TRIANGLES, 0, 6);
}

void window_draw_button(window_scene_t* s, worker_args_t* args) { (void)args; }

void window_render(window_scene_t* s, worker_args_t* args) {
	vxair_cmd_set_viewport(s->ctx, 0.0f, 0.0f, args->screen_w,
	                       args->screen_h, 0.0f, 0.0f);
	vxair_bind_blend(s->ctx, s->alpha_blend_id);

	window_shadow_render(s, args);

	float centerX = s->x + s->w / 2.0f;
	float centerY = s->y + s->h / 2.0f;
	float radius = 8.0f;
	float feather = 1.5f;
	float invFeather = 1.0f / feather;
	float titleBarHeight = s->titlebar_height;
	float dim_factor = 1.0f;

	rgba8_t tb = rgba_from_hex(0xFAFAFAFF);

	{

		vxair_cmd_bind_shader(s->ctx, s->vs);
		vxair_cmd_bind_shader(s->ctx, s->fs);

		vxair_cmd_bind_texture(s->ctx, 0, s->fb_tex);
		vxair_cmd_set_sampler_filter(s->ctx, 0, VXAIR_FILTER_NEAREST,
		                             VXAIR_FILTER_NEAREST);

		float uniform[] = {
		    2.0f / args->screen_w,
		    2.0f / args->screen_h,
		    -1.0f,
		    -1.0f, /* CONST 0: NDC scale/bias */
		    0.0f,
		    1.0f,
		    0.0f,
		    0.0f, /* CONST 1: z, w */
		    s->x,
		    s->y,
		    s->w,
		    s->h, /* CONST 2: window pos & size */
		};
		vxair_cmd_set_constant_buffer(s->ctx, VXAIR_SHADER_VERTEX, 0,
		                              sizeof(uniform), uniform);

		float border_thickness_px = 1.2f;
		float half_thickness = border_thickness_px / 2.0f;
		float inv_border_thickness = -1.0f / half_thickness;

		rgba8_t border_col = rgba_from_hex(0xa6a6a6FF);

		float innerPadding = 0.0f;
		float innerTopOffset = titleBarHeight + innerPadding;

		float innerLeft = s->x + innerPadding;
		float innerRight = s->x + s->w - innerPadding;
		float innerTop = s->y + titleBarHeight + innerPadding;
		float innerBottom = s->y + s->h - innerPadding;

		float innerCenterX = (innerLeft + innerRight) * 0.5f;
		float innerCenterY = (innerTop + innerBottom) * 0.5f;
		float innerHalfW = (innerRight - innerLeft) * 0.5f;
		float innerHalfH = (innerBottom - innerTop) * 0.5f;

		float innerRadius = 0.0f;
		if (innerRadius < 0.0f)
			innerRadius = 0.0f;

		float innerBx = innerHalfW - innerRadius;
		if (innerBx < 0.0f)
			innerBx = 0.0f;
		float innerBy = innerHalfH - innerRadius;
		if (innerBy < 0.0f)
			innerBy = 0.0f;

		float frag_uniform[] = {
		    centerX, centerY, -invFeather, 0.5f,
		    /* CONST 0 */
		    (s->w / 2.0f) - radius, (s->h / 2.0f) - radius, 0.0f,
		    radius,
		    /* CONST 1 */
		    tb.r / 255.0f, tb.g / 255.0f, tb.b / 255.0f,
		    s->y + titleBarHeight,
		    /* CONST 2 */
		    1.0f, 1.0f, 1.0f, 1.0f, /* CONST 3: inner bg color */
		    border_col.r / 255.0f, border_col.g / 255.0f,
		    border_col.b / 255.0f, inv_border_thickness,
		    /* CONST 4: border */
		    s->use_texture,
		    dim_factor, // dim_factor
		    0.333f, 1.0,
		    /* CONST 5: texture flag */
		    innerPadding, innerTopOffset, 0, 0,
		    /* CONST 6 */
		    innerHalfW, innerHalfH, 0.5f / innerHalfW, 0.5f / innerHalfH,
		    /* CONST 7 */
		    innerCenterX, innerCenterY, innerBx, innerBy,
		    /* CONST 8 */
		    innerRadius, 0.0f, 0.0f, 0.55f,
		    /* CONST 9 */
		};

		vxair_cmd_set_constant_buffer(s->ctx, VXAIR_SHADER_FRAGMENT, 0,
		                              sizeof(frag_uniform),
		                              frag_uniform);

		vxair_vertex_element_t elems[2] = {
		    {0, VXAIR_VERTEX_FORMAT_FLOAT2},
		    {8, VXAIR_VERTEX_FORMAT_FLOAT4},
		};
		vxair_cmd_bind_vertex_elements(s->ctx, 2, elems);
		vxair_cmd_bind_vertex_buffer(s->ctx, s->vbo, 6 * sizeof(float));
		vxair_cmd_draw_arrays(s->ctx, VXAIR_PRIMITIVE_TRIANGLES, 0, 6);
	}
	// font
	{
		vxair_bind_blend(s->ctx, s->alpha_blend_id);

		tb.a *= dim_factor;
		vxui_text_desc_t text_opts = {.scale = 1.0f,
		                              .color = 0x000000FF,
		                              .bg_color = rgba_to_hex(tb) & 0xFFFFFF00,
		                              .align = VXUI_ALIGN_CENTER};

		float gy = s->y + (titleBarHeight - 14.0f) / 2.0f;

		static vxair_buffer_t* title_vbo = NULL;
		vxui_draw_text(args->text_renderer, args->dev, s->ctx,
		               args->font, "Terminal", centerX, gy, &text_opts,
		               &title_vbo);
	}
}
