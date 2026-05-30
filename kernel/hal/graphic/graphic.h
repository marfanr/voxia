#ifndef __HAL__GRAPHIC__GRAPHIC_H__
#define __HAL__GRAPHIC__GRAPHIC_H__

#include <type.h>

typedef struct {
	uint8_t b, g, r, a;
} pixel_t;

void put_pixel(int x, int y, uint32_t color);
void put_pixel_alpha(int x, int y, pixel_t src);
void putc(char c, int x, int y, uint32_t fg, uint32_t bg);
void putc_utf8(const char *s, int col, int row, uint32_t fg, uint32_t bg);
int utf8_char_len(uint8_t c);
void put_pixel_alpha_fast(int x, int y, pixel_t src);

uint32_t vxGetWidth(void);
uint32_t vxGetHeight(void);
void vxScroll(int px);
void clear_screen(uint32_t color);
uint32_t screen_cols(void);
uint32_t screen_rows(void);

void fill_rect(int x, int y, int w, int h, uint32_t color);

#define FONT_SIZE 14

#endif // __HAL__GRAPHIC__GRAPHIC_H__