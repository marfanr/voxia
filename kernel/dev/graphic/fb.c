#include "fb.h"

// #define SSFN_CONSOLEBITMAP_TRUECOLOR
// #define SSFN_NOIMPLEMENTATION
#include <dev/initrd/initrd.h>
#include <libk/serial.h>
// #include <libk/ssfn.h>

/**
 * @brief framebuffer info
 *
 */
struct stivale2_struct_tag_framebuffer *framebuffer_info_;

// initialize framebuffer
void
fb_init (struct stivale2_struct_tag_framebuffer *framebuffer_info,
         uint32_t bgcolor)
{
    framebuffer_info_ = framebuffer_info;
    fb_cls (bgcolor);

    // init ssfn
    // ssfn_src = (ssfn_font_t *)initrd_find_file ("fonts/unifont.sfn.gz");
    // ssfn_dst.ptr = (uint8_t *)framebuffer_info_->framebuffer_addr;
    // ssfn_dst.w = framebuffer_info_->framebuffer_width;
    // ssfn_dst.h = framebuffer_info_->framebuffer_height;
    // ssfn_dst.p = framebuffer_info_->framebuffer_pitch;
    // ssfn_dst.x = ssfn_dst.y = 0;
    // ssfn_dst.fg = 0xFFFFFF;
    // ssfn_dst.bg = 0x000000;
}

// put pixel on screen
void
fb_put_pixel (uint32_t x, uint32_t y, uint32_t color)
{
    *((uint8_t *)framebuffer_info_->framebuffer_addr
      + y * framebuffer_info_->framebuffer_pitch + x * 4)
        = color & 0xFF;
    *((uint8_t *)framebuffer_info_->framebuffer_addr
      + y * framebuffer_info_->framebuffer_pitch + x * 4 + 1)
        = (color >> 8) & 0xFF;
    *((uint8_t *)framebuffer_info_->framebuffer_addr
      + y * framebuffer_info_->framebuffer_pitch + x * 4 + 2)
        = (color >> 16) & 0xFF;
    *((uint8_t *)framebuffer_info_->framebuffer_addr
      + y * framebuffer_info_->framebuffer_pitch + x * 4 + 3)
        = (color >> 24) & 0xFF;
}

// clear screen
void
fb_cls (uint32_t bgcolor)
{
    for (uint32_t y = 0; y < framebuffer_info_->framebuffer_height; y++)
        {
            for (uint32_t x = 0; x < framebuffer_info_->framebuffer_width; x++)
                {
                    fb_put_pixel (x, y, bgcolor);
                }
        }
}

void
fb_scroll_up (uint32_t height)
{
    // for (int c = 16; c < framebuffer_info_->framebuffer_height; c++)
    //     {
    //         for (int r = 0; r < framebuffer_info_->framebuffer_width; r++)
    //             {
    //                 size_t cur = c
    //                                  * (framebuffer_info_->framebuffer_pitch
    //                                     / sizeof (uint32_t))
    //                              + r;
    //                 uint32_t cur_color = *(
    //                     (uint32_t *)framebuffer_info_->framebuffer_addr +
    //                     cur);
    //                 *((uint32_t *)framebuffer_info_->framebuffer_addr + cur)
    //                     = ssfn_dst.bg;
    //                 size_t new_index
    //                     = (c - 16)
    //                           * (framebuffer_info_->framebuffer_pitch
    //                              / sizeof (uint32_t))
    //                       + r;
    //                 *((uint32_t *)framebuffer_info_->framebuffer_addr
    //                   + new_index)
    //                     = cur_color;
    //             }
    //     }
}

void
fb_put_char (char c, int x, int y, uint32_t fg, uint32_t bg)
{
    // ssfn_dst.fg = fg;
    // ssfn_dst.bg = bg;
    // ssfn_dst.x = x * 8;
    // ssfn_dst.y = y * 15;
    // ssfn_putc (c);
}

uint32_t
fb_get_height ()
{
    return framebuffer_info_->framebuffer_height;
}