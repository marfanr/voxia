#include "framebuffer.h"
#define SSFN_CONSOLEBITMAP_1COLOR
#define SSFN_NOIMPLEMENTATION
#include <libk/ssfn.h>

static struct framebuffer *g__fb;

void
framebuffer_setup (struct framebuffer *fb)
{
    g__fb = fb;

    ssfn_src = (ssfn_font_t *)fb->font;
    ssfn_dst.ptr = (uint8_t *)fb->addr;
    ssfn_dst.w = fb->width;
    ssfn_dst.h = fb->height;
    ssfn_dst.p = fb->pitch;
    ssfn_dst.x = ssfn_dst.y = 0;
    ssfn_dst.fg = 0xFFFFFF;
    ssfn_dst.bg = 0x000000;
}

void
putc (char c, int x, int y, uint32_t fg, uint32_t bg)
{
    ssfn_dst.fg = fg;
    ssfn_dst.bg = bg;
    ssfn_dst.x = x * 7;
    ssfn_dst.y = y * 15;
    ssfn_putc (c);
}

void
putpx (uint32_t x, uint32_t y, uint32_t color)
{
    *((uint8_t *)g__fb->addr + y * g__fb->pitch + x * 4) = color & 0xFF;
    *((uint8_t *)g__fb->addr + y * g__fb->pitch + x * 4 + 1)
        = (color >> 8) & 0xFF;
    *((uint8_t *)g__fb->addr + y * g__fb->pitch + x * 4 + 2)
        = (color >> 16) & 0xFF;
    *((uint8_t *)g__fb->addr + y * g__fb->pitch + x * 4 + 3)
        = (color >> 24) & 0xFF;
}
