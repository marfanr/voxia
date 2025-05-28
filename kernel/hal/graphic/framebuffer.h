#ifndef __HAL_GRAPHIC_FRAMEBUFFER_H__
#define __HAL_GRAPHIC_FRAMEBUFFER_H__

#include <libk/type.h>

struct framebuffer
{
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t addr;
    uint8_t *font;
};

void framebuffer_setup (struct framebuffer *fb);
void putpx (uint32_t x, uint32_t y, uint32_t color);
void putc (char c, int x, int y, uint32_t fg, uint32_t bg);
#endif // __HAL_GRAPHIC_FRAMEBUFFER_H__