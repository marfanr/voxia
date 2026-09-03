#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <vxui_text.h>

// ── FONT SHADER STRINGS ──
static const char* FONT_VS = "VERT\n"
                             "DCL IN[0]\n"
                             "DCL IN[1]\n"
                             "DCL OUT[0], POSITION\n"
                             "DCL OUT[1], GENERIC[0]\n"
                             "DCL CONST[0..1]\n"
                             "DCL TEMP[0]\n"
                             "MUL TEMP[0].x, IN[0].xxxx, CONST[0].xxxx\n"
                             "ADD TEMP[0].x, TEMP[0].xxxx, CONST[0].zzzz\n"
                             "MUL TEMP[0].y, IN[0].yyyy, CONST[0].yyyy\n"
                             "ADD TEMP[0].y, TEMP[0].yyyy, CONST[0].wwww\n"
                             "MOV TEMP[0].z, CONST[1].xxxx\n"
                             "MOV TEMP[0].w, CONST[1].yyyy\n"
                             "MOV OUT[0], TEMP[0]\n"
                             "MOV OUT[1], IN[1]\n"
                             "END\n";

static const char* FONT_FS =
    "FRAG\n"
    "DCL IN[0], GENERIC[0], PERSPECTIVE\n"
    "DCL OUT[0], COLOR\n"
    "DCL SAMP[0]\n"
    "DCL CONST[0..3]\n"
    "DCL TEMP[0..2]\n"
    "TEX TEMP[0], IN[0], SAMP[0], 2D\n"
    "POW TEMP[0].x, TEMP[0].xxxx, CONST[1].xxxx\n"
    "POW TEMP[0].y, TEMP[0].yyyy, CONST[1].xxxx\n"
    "POW TEMP[0].z, TEMP[0].zzzz, CONST[1].xxxx\n"
    "POW TEMP[0].w, TEMP[0].wwww, CONST[1].xxxx\n"
    "MUL TEMP[1].x, TEMP[0].wwww, CONST[2].xxxx\n"
    "MUL TEMP[1].y, TEMP[0].wwww, CONST[2].yyyy\n"
    "MUL TEMP[1].z, TEMP[0].wwww, CONST[2].zzzz\n"
    "SUB TEMP[2].x, CONST[3].wwww, TEMP[0].wwww\n"
    "SUB TEMP[2].y, CONST[3].wwww, TEMP[0].wwww\n"
    "SUB TEMP[2].z, CONST[3].wwww, TEMP[0].wwww\n"
    "MAD TEMP[1].x, TEMP[2].xxxx, CONST[0].xxxx, TEMP[1].xxxx\n"
    "MAD TEMP[1].y, TEMP[2].yyyy, CONST[0].yyyy, TEMP[1].yyyy\n"
    "MAD TEMP[1].z, TEMP[2].zzzz, CONST[0].zzzz, TEMP[1].zzzz\n"
    "SUB TEMP[2].w, CONST[3].wwww, CONST[0].wwww\n"
    "MUL TEMP[2].x, CONST[2].xxxx, TEMP[2].wwww\n"
    "MUL TEMP[2].y, CONST[2].yyyy, TEMP[2].wwww\n"
    "MUL TEMP[2].z, CONST[2].zzzz, TEMP[2].wwww\n"
    "MAD OUT[0].x, TEMP[1].xxxx, CONST[0].wwww, TEMP[2].xxxx\n"
    "MAD OUT[0].y, TEMP[1].yyyy, CONST[0].wwww, TEMP[2].yyyy\n"
    "MAD OUT[0].z, TEMP[1].zzzz, CONST[0].wwww, TEMP[2].zzzz\n"
    "MAD OUT[0].w, TEMP[0].wwww, TEMP[2].wwww, CONST[0].wwww\n"
    "END\n";

typedef struct {
	float u0, v0, u1, v1; // UV rect di dalam atlas
	int width, height;    // ukuran glyph dalam pixel
	int bearing_x, bearing_y;
	int advance; // dalam 1/64 px (dari FT) -> nanti dibagi 64
	int glyph_index;
} glyph_info_t;

struct vxui_font_t {
	vxair_texture_t* tex;
	glyph_info_t glyphs[128]; // ASCII printable aja dulu cukup
	int atlas_w, atlas_h;
	float ascender;
	FT_Face face;
};

struct vxui_text_renderer_t {
	vxair_shader_t* vs;
	vxair_shader_t* fs;
	float screen_w;
	float screen_h;
};

vxui_text_renderer_t*
vxui_text_renderer_create(vxair_device_t* dev, float screen_w, float screen_h) {
	vxui_text_renderer_t* r = calloc(1, sizeof(vxui_text_renderer_t));
	r->vs = vxair_shader_create(dev, VXAIR_SHADER_VERTEX, FONT_VS);
	r->fs = vxair_shader_create(dev, VXAIR_SHADER_FRAGMENT, FONT_FS);
	r->screen_w = screen_w;
	r->screen_h = screen_h;
	return r;
}

void vxui_text_renderer_set_screen_size(vxui_text_renderer_t* renderer,
                                        float screen_w, float screen_h) {
	if (renderer) {
		renderer->screen_w = screen_w;
		renderer->screen_h = screen_h;
	}
}

void vxui_text_renderer_destroy(vxair_device_t* dev,
                                vxui_text_renderer_t* renderer) {
	if (renderer) {
		free(renderer);
	}
}

FT_Face vxui_font_face_load(const char* font_path, int pixel_size) {
	printf("[vxui] vxui_font_face_load start\n");
	FT_Library ft_ = NULL;
	if (FT_Init_FreeType(&ft_)) {
		printf("Gagal inisialisasi FreeType\n");
		return NULL;
	}
	printf("[vxui] FT_Init_FreeType success\n");

	FT_Library_SetLcdFilter(ft_, FT_LCD_FILTER_DEFAULT);
	printf("[vxui] properties set\n");

	FT_Face face;
	if (FT_New_Face(ft_, font_path, 0, &face)) {
		printf("font load failed\n");
		return NULL;
	}
	printf("[vxui] FT_New_Face success\n");
	FT_Set_Pixel_Sizes(face, 0, pixel_size);
	printf("[vxui] vxui_font_face_load end\n");
	return face;
}

vxui_font_t* vxui_font_load(vxair_device_t* dev, FT_Face face_, int first,
                            int last, font_style_flags_t style) {
	vxui_font_t* atlas = calloc(1, sizeof(vxui_font_t));

	int atlas_w = 1024, atlas_h = 1024;
	uint8_t* pixels = calloc(atlas_w * atlas_h * 4, 1);

	int pen_x = 0, pen_y = 0, row_h = 0;

	bool want_italic = (style & FONT_STYLE_ITALIC) != 0;
	bool want_bold = (style & FONT_STYLE_BOLD) != 0;

	if (want_italic) {
		FT_Matrix shear = {
		    0x10000,
		    0x0366A, /* ~12 derajat shear */
		    0x0,
		    0x10000,
		};
		FT_Set_Transform(face_, &shear, NULL);
	} else {
		FT_Set_Transform(face_, NULL, NULL);
	}

	for (int c = first; c <= last; c++) {
		if (FT_Load_Char(face_, c,
		                 FT_LOAD_DEFAULT | FT_LOAD_TARGET_LCD))
			continue;

		FT_GlyphSlot g = face_->glyph;

		if (want_bold) {
			FT_Outline_Embolden(&g->outline,
			                    1 << 6); /* strength 1px */
		}

		if (FT_Render_Glyph(g, FT_RENDER_MODE_LCD))
			continue;

		int gw = g->bitmap.width / 3;
		int gh = g->bitmap.rows;

		if (pen_x + gw >= atlas_w) {
			pen_x = 0;
			pen_y += row_h + 8;
			row_h = 0;
		}

		for (int yy = 0; yy < gh; yy++) {
			uint32_t* dst =
			    (uint32_t*)pixels + (pen_y + yy) * atlas_w + pen_x;
			uint8_t* src = g->bitmap.buffer + yy * g->bitmap.pitch;

			for (int xx = 0; xx < gw; xx++) {
				uint8_t r = src[xx * 3 + 0];
				uint8_t g_val = src[xx * 3 + 1];
				uint8_t b = src[xx * 3 + 2];

				uint8_t a =
				    (uint8_t)(((int)r + (int)g_val + (int)b) /
				              3);
				dst[xx] =
				    (a << 24) | (b << 16) | (g_val << 8) | r;
			}
		}

		glyph_info_t* gi = &atlas->glyphs[c];

		gi->u0 = (float)pen_x / atlas_w;
		gi->v0 = (float)pen_y / atlas_h;
		gi->u1 = (float)(pen_x + gw) / atlas_w;
		gi->v1 = (float)(pen_y + gh) / atlas_h;
		gi->glyph_index = FT_Get_Char_Index(face_, c);

		gi->width = gw;
		gi->height = gh;
		gi->bearing_x = g->bitmap_left;
		gi->bearing_y = g->bitmap_top;
		gi->advance = g->advance.x;

		pen_x += gw + 8;
		row_h = row_h > gh ? row_h : gh;
	}

	FT_Set_Transform(face_, NULL, NULL);

	atlas->tex = vxair_texture_create(dev, atlas_w, atlas_h,
	                                  VXAIR_FORMAT_RGBA8, pixels);
	atlas->atlas_w = atlas_w;
	atlas->atlas_h = atlas_h;

	atlas->face = face_;
	if (face_->size) {
		atlas->ascender = face_->size->metrics.ascender / 64.0f;
	} else {
		atlas->ascender = 14.0f;
	}

	free(pixels);
	return atlas;
}

vxui_font_t* vxui_font_create(vxair_device_t* dev, const char* font_path,
                              int pixel_size, int first, int last,
                              font_style_flags_t style) {
	FT_Face face = vxui_font_face_load(font_path, pixel_size);
	if (!face)
		return NULL;
	return vxui_font_load(dev, face, first, last, style);
}

void vxui_font_destroy(vxair_device_t* dev, vxui_font_t* font) {
	if (font) {
		if (font->tex) {
			// vxair_texture_destroy(dev, font->tex); // Not yet
			// implemented in vxair
		}
		free(font);
	}
}

void vxui_text_measure(vxui_font_t* font, const char* text, float scale,
                       float* out_w, float* out_h) {
	if (!font || !text) {
		if (out_w)
			*out_w = 0;
		if (out_h)
			*out_h = 0;
		return;
	}

	int n = strlen(text);
	float pen_x = 0;
	float max_x = 0;
	float y = 0;
	FT_UInt prev_index = 0;

	for (int i = 0; i < n; i++) {
		if (text[i] == '\n') {
			if (pen_x > max_x)
				max_x = pen_x;
			pen_x = 0;
			y += font->ascender * scale;
			prev_index = 0;
			continue;
		}

		glyph_info_t* gi = &font->glyphs[(uint8_t)text[i]];

		if (prev_index != 0 && gi->glyph_index != 0 && font->face) {
			FT_Vector delta;
			FT_Get_Kerning(font->face, prev_index, gi->glyph_index,
			               FT_KERNING_UNFITTED, &delta);
			pen_x += (delta.x >> 6) * scale;
		}
		prev_index = gi->glyph_index;

		pen_x += (gi->advance / 64.0f) * scale;
	}
	if (pen_x > max_x)
		max_x = pen_x;

	if (out_w)
		*out_w = max_x;
	if (out_h)
		*out_h = y + font->ascender * scale;
}

void vxui_draw_text(vxui_text_renderer_t* renderer, vxair_device_t* dev,
                    vxair_context_t* ctx, vxui_font_t* font, const char* text,
                    float x, float y, const vxui_text_desc_t* desc,
                    vxair_buffer_t** pvbo) {
	if (!renderer || !font || !text || !pvbo)
		return;

	float scale = desc ? desc->scale : 1.0f;
	vxui_align_t align = desc ? desc->align : VXUI_ALIGN_LEFT;

	float start_x = x;
	if (align != VXUI_ALIGN_LEFT) {
		float w, h;
		vxui_text_measure(font, text, scale, &w, &h);
		if (align == VXUI_ALIGN_CENTER) {
			start_x = x - w / 2.0f;
		} else if (align == VXUI_ALIGN_RIGHT) {
			start_x = x - w;
		}
	}

	int n = strlen(text);
	size_t vert_bytes = n * 6 * 4 * sizeof(float);
	float* verts = malloc(vert_bytes);
	int vcount = 0;

	float pen_x = start_x;
	FT_UInt prev_index = 0;
	float ascender = font->ascender;

	for (int i = 0; i < n; i++) {
		if (text[i] == '\n') {
			pen_x = start_x;
			y += ascender * scale;
			prev_index = 0;
			continue;
		}

		glyph_info_t* gi = &font->glyphs[(uint8_t)text[i]];

		float advance = (gi->advance / 64.0f) * scale;
		float offset_x = 0;
		if (desc && desc->fixed_advance > 0.0f) {
			advance = desc->fixed_advance;
			/* center the character roughly if it is smaller */
			float char_w = gi->width * scale;
			offset_x = (desc->fixed_advance - char_w) / 2.0f;
			if (offset_x < 0) offset_x = 0;
		} else if (prev_index != 0 && gi->glyph_index != 0 && font->face) {
			FT_Vector delta;
			FT_Get_Kerning(font->face, prev_index, gi->glyph_index,
			               FT_KERNING_UNFITTED, &delta);
			pen_x += (delta.x >> 6) * scale;
		}
		prev_index = gi->glyph_index;

		float gx = pen_x + (gi->bearing_x * scale) + offset_x;
		float gy_raw = y + (ascender * scale) - (gi->bearing_y * scale);
		float gy = floorf(gy_raw + 0.5f);

		float gw = gi->width * scale;
		float gh = gi->height * scale;

		float quad[24] = {
		    gx,     gy,     gi->u0,  gi->v0,  gx,      gy + gh,
		    gi->u0, gi->v1, gx + gw, gy + gh, gi->u1,  gi->v1,

		    gx,     gy,     gi->u0,  gi->v0,  gx + gw, gy + gh,
		    gi->u1, gi->v1, gx + gw, gy,      gi->u1,  gi->v0,
		};
		memcpy(verts + vcount * 4, quad, sizeof(quad));
		vcount += 6;

		pen_x += advance;
	}

	if (vcount == 0) {
		free(verts);
		return;
	}

	size_t needed = vcount * 4 * sizeof(float);
	vxair_buffer_t* vbo = *pvbo;

	if (!vbo || vxair_buffer_get_size(vbo) < needed) {
		if (vbo)
			vxair_buffer_destroy(dev, vbo);
		
		size_t new_cap = needed * 2;
		// Beri ukuran minimal 1MB untuk menghindari realokasi berulang 
		// yang bisa memicu race-condition di GPU/virgl.
		if (new_cap < 1024 * 1024) {
			new_cap = 1024 * 1024; 
		}

		vbo = vxair_buffer_create(dev, new_cap, NULL);
		*pvbo = vbo;
		if (!vbo) {
			free(verts);
			return;
		}
	}

	vxair_buffer_update(dev, vbo, 0, needed, verts);
	free(verts);

	vxair_cmd_bind_shader(ctx, renderer->vs);
	vxair_cmd_bind_shader(ctx, renderer->fs);

	float font_vs_uniform[] = {
	    2.0f / renderer->screen_w,
	    2.0f / renderer->screen_h,
	    -1.0f,
	    -1.0f, /* CONST 0: NDC scale/bias */
	    0.0f,
	    1.0f,
	    0.0f,
	    0.0f, /* CONST 1: z, w */
	};
	vxair_cmd_set_constant_buffer(ctx, VXAIR_SHADER_VERTEX, 0,
	                              sizeof(font_vs_uniform), font_vs_uniform);

	float bg_r = 0.0f, bg_g = 0.0f, bg_b = 0.0f, bg_a = 1.0f;
	float color_r = 1.0f, color_g = 1.0f, color_b = 1.0f, color_a = 1.0f;
	if (desc) {
		color_r = ((desc->color >> 24) & 0xFF) / 255.0f;
		color_g = ((desc->color >> 16) & 0xFF) / 255.0f;
		color_b = ((desc->color >> 8) & 0xFF) / 255.0f;
		color_a = ((desc->color >> 0) & 0xFF) / 255.0f;

		bg_r = ((desc->bg_color >> 24) & 0xFF) / 255.0f;
		bg_g = ((desc->bg_color >> 16) & 0xFF) / 255.0f;
		bg_b = ((desc->bg_color >> 8) & 0xFF) / 255.0f;
		bg_a = ((desc->bg_color >> 0) & 0xFF) / 255.0f;
	}

	float font_uniforms[] = {
	    bg_r,    bg_g,    bg_b,   bg_a,    2.0f, // CONST[1].x is Gamma
	    0.0f,    0.0f,    0.0f,   color_r, color_g,
	    color_b, color_a, /* CONST 2: color/alpha */
	    0.587f,  0.299f,  0.114f, 1.0f};
	vxair_cmd_set_constant_buffer(ctx, VXAIR_SHADER_FRAGMENT, 0,
	                              sizeof(font_uniforms), font_uniforms);

	vxair_cmd_set_sampler_filter(ctx, 0, VXAIR_FILTER_NEAREST,
	                             VXAIR_FILTER_NEAREST);

	vxair_cmd_bind_texture(ctx, 0, font->tex);
	vxair_vertex_element_t elem[2] = {
	    {0, VXAIR_VERTEX_FORMAT_FLOAT2},
	    {8, VXAIR_VERTEX_FORMAT_FLOAT2},
	};
	vxair_cmd_bind_vertex_elements(ctx, 2, elem);
	vxair_cmd_bind_vertex_buffer(ctx, vbo, 4 * sizeof(float));
	vxair_cmd_draw_arrays(ctx, VXAIR_PRIMITIVE_TRIANGLES, 0, vcount);
}