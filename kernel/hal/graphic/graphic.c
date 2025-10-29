#include "graphic.h"
#include "framebuffer.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "memory/entry.h"
#include "memory/vm_manager.h"
#include <libk/serial.h>

#define SSFN_CONSOLEBITMAP_1COLOR
#define SSFN_NOIMPLEMENTATION
#include <libk/ssfn.h>

framebuffer_t *g__fb;

INIT(graphic)
{
    g__fb = &ctx->framebuffer;

    // loading font
    int                   font_fd = vfs_open("/dev/initrd/fonts/unifont.sfn", OPEN_MODE_R);
    struct vfs_file_stats stats;
    vfs_fstat(font_fd, &stats);
    serial_trace("(init) font size : %d\n", stats.size);
    serial_trace("(init) font size :%dMb\n", stats.size / 1024 / 1024);

    uint8_t *font = (uint8_t *)kalloc(stats.size);
    serial_trace("font address : 0x%x\n", font);
    vfs_read(font_fd, font, stats.size);
    serial_trace("read font success\n");

    // overide fb addr
    for (uint64_t i = 0; i < ctx->memory.memory_entries; i++)
    {
        memory_entry_t *entry = &ctx->memory.memory_map[i];
        if (entry->type == ENTRY_MMAP_FRAMEBUFFER)
        {
            paging_mmap_fill(paging_get_highest_page_map(), 0xFFFFFA0000000000, entry->base,
                             entry->length / PAGE_SIZE, 0b111);
            vma_register(entry->base, 0xFFFFFA0000000000, entry->length / PAGE_SIZE);
            vma_tree_add(VMA_REGION_A, 0xFFFFFA0000000000, 0xFFFFFA0000000000 + entry->length);
            g__fb->framebuffer_addr = 0xFFFFFA0000000000;
            break;
        }
    }

    // init ssfn for early boot
    ssfn_src     = (ssfn_font_t *)font;
    ssfn_dst.ptr = (uint8_t *)g__fb->framebuffer_addr;
    ssfn_dst.w   = g__fb->framebuffer_width;
    ssfn_dst.h   = g__fb->framebuffer_height;
    ssfn_dst.p   = g__fb->framebuffer_pitch;
    ssfn_dst.x = ssfn_dst.y = 0;
    ssfn_dst.fg             = 0xFFFFFF;
    ssfn_dst.bg             = 0x000000;
}

void
putc(char c, int x, int y, uint32_t fg, uint32_t bg)
{
    ssfn_dst.fg = fg;
    ssfn_dst.bg = bg;
    ssfn_dst.x  = x * 7;
    ssfn_dst.y  = y * 15;
    ssfn_putc(c);
}

void
put_pixel(int x, int y, uint32_t color)
{
    pixel_t *pixel =
        (pixel_t *)((uint8_t *)g__fb->framebuffer_addr + y * g__fb->framebuffer_pitch + x * 4);
    pixel->r = color & 0xFF;
    pixel->g = (color >> 8) & 0xFF;
    pixel->b = (color >> 16) & 0xFF;
    pixel->a = (color >> 24) & 0xFF;
}

void
put_pixel_alpha(int x, int y, pixel_t src)
{
    if (x < 0 || y < 0 || x >= g__fb->framebuffer_width || y >= g__fb->framebuffer_height)
        return;

    uint32_t *dst_ptr =
        (uint32_t *)((uint8_t *)g__fb->framebuffer_addr + y * g__fb->framebuffer_pitch + x * 4);
    uint32_t dst_color = *dst_ptr;

    pixel_t dst = {.b = dst_color & 0xFF,
                   .g = (dst_color >> 8) & 0xFF,
                   .r = (dst_color >> 16) & 0xFF,
                   .a = 0xFF};

    uint8_t a = src.a;

    uint8_t r = (src.r * a + dst.r * (255 - a)) / 255;
    uint8_t g = (src.g * a + dst.g * (255 - a)) / 255;
    uint8_t b = (src.b * a + dst.b * (255 - a)) / 255;

    uint32_t color = 0;

    uint32_t r_val = (r * ((1 << g__fb->red_mask_size) - 1)) / 255;
    uint32_t g_val = (g * ((1 << g__fb->green_mask_size) - 1)) / 255;
    uint32_t b_val = (b * ((1 << g__fb->blue_mask_size) - 1)) / 255;

    color |= (r_val << g__fb->red_mask_shift);
    color |= (g_val << g__fb->green_mask_shift);
    color |= (b_val << g__fb->blue_mask_shift);

    *dst_ptr = color;
}