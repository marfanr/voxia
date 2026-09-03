#ifndef __VXUI_TEXT_H_
#define __VXUI_TEXT_H_

#include <stdint.h>
#include <vxair.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H
#include FT_MODULE_H
#include FT_LCD_FILTER_H

typedef enum {
	FONT_STYLE_REGULAR = 0,
	FONT_STYLE_BOLD = 1 << 0,
	FONT_STYLE_ITALIC = 1 << 1,
	FONT_STYLE_BOLD_ITALIC = FONT_STYLE_BOLD | FONT_STYLE_ITALIC,
} font_style_flags_t;

typedef enum {
	VXUI_ALIGN_LEFT,
	VXUI_ALIGN_CENTER,
	VXUI_ALIGN_RIGHT
} vxui_align_t;

typedef struct vxui_font_t vxui_font_t;
typedef struct vxui_text_renderer_t vxui_text_renderer_t;

// --- Text Renderer ---
vxui_text_renderer_t* vxui_text_renderer_create(vxair_device_t* dev,
                                                float screen_w, float screen_h);
void vxui_text_renderer_set_screen_size(vxui_text_renderer_t* renderer,
                                        float screen_w, float screen_h);
void vxui_text_renderer_destroy(vxair_device_t* dev,
                                vxui_text_renderer_t* renderer);

// --- Font Management ---
FT_Face vxui_font_face_load(const char* font_path, int pixel_size);
vxui_font_t* vxui_font_load(vxair_device_t* dev, FT_Face face_, int first,
                            int last, font_style_flags_t style);
vxui_font_t* vxui_font_create(vxair_device_t* dev, const char* font_path,
                              int pixel_size, int first, int last,
                              font_style_flags_t style);
void vxui_font_destroy(vxair_device_t* dev, vxui_font_t* font);

// --- Text Operations ---
void vxui_text_measure(vxui_font_t* font, const char* text, float scale,
                       float* out_w, float* out_h);

typedef struct {
	float scale;
	uint32_t color;
	uint32_t bg_color;
	vxui_align_t align;
	float fixed_advance;
} vxui_text_desc_t;

// Render teks
void vxui_draw_text(vxui_text_renderer_t* renderer, vxair_device_t* dev,
                    vxair_context_t* ctx, vxui_font_t* font, const char* text,
                    float x, float y, const vxui_text_desc_t* desc,
                    vxair_buffer_t** pvbo);

#endif // __VXUI_TEXT_H_