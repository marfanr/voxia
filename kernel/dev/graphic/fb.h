#ifndef __DEV__GRAPHIC__FB_H__
#define __DEV__GRAPHIC__FB_H__

#include <libk/stivale2.h>
#include <libk/type.h>

#define FB_COLOR_BLACK 0x00000000

void fb_init(struct stivale2_struct_tag_framebuffer *fb, uint32_t bgcolor);
void fb_cls(uint32_t bgcolor);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_put_char(char c, int x, int y, uint32_t fg, uint32_t bg);
void fb_scroll_up(uint32_t height);
uint32_t fb_get_height();

#endif