#include "graphic.h"
#include "framebuffer.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "memory/entry.h"
#include "memory/vm_manager.h"
#include "vfs/enum.h"
#include "vfs/file.h"
#include "vfs/vfs.h"
#include <libk/serial.h>

#define SSFN_CONSOLEBITMAP_1COLOR
#define SSFN_NOIMPLEMENTATION
#include <libk/ssfn.h>

volatile framebuffer_t* g__fb;

INIT(graphic) {
	g__fb = &ctx->framebuffer;
	LOG_DEBUG("FB", "Fb addr 0x%lx", g__fb->framebuffer_addr);

	if (g__fb->framebuffer_addr == 0)
		return;

	// loading font
	dentry_ptr font_dentry;
	uint8_t* font_buff = 0;
	if (vxResolveDentry("/init/fonts/unifont.sfn", 0, &font_dentry, 0)
	    == VFS_OK) {
		LOG_DEBUG("VFS", "opened %s (%d kb)", font_dentry->name->c_str,
			  font_dentry->vnode->size / 1024);
		font_buff = (uint8_t*) kalloc(font_dentry->vnode->size);
		((vops_file_t*) font_dentry->vnode->ops)
			->read(font_dentry->vnode, font_buff,
			       font_dentry->vnode->size, 0);
		ssfn_src = (ssfn_font_t*) font_buff;
	} else {
		LOG_WARN("VFS", "failed to load font");
	}

	// overide fb addr
	for (uint64_t i = 0; i < ctx->memory.memory_entries; i++) {
		memory_entry_t* entry = &ctx->memory.memory_map[i];
		if (entry->type == ENTRY_MMAP_FRAMEBUFFER) {
			vxMultipleMmap(paging_get_highest_page_map(),
				       0xFFFFFA0000000000, entry->base,
				       entry->length / PAGE_SIZE, 0b111);
			vma_register(entry->base, 0xFFFFFA0000000000,
				     entry->length / PAGE_SIZE);
			// vma_tree_add(VMA_REGION_A, 0xFFFFFA0000000000,
			// 0xFFFFFA0000000000 +entry->length);
			g__fb->framebuffer_addr = 0xFFFFFA0000000000;
			break;
		}
	}

	serial_trace("new framebuffer 0x%lx\n", g__fb->framebuffer_addr);

	// init ssfn for early boot
	ssfn_dst.ptr = (uint8_t*) g__fb->framebuffer_addr;
	ssfn_dst.w = g__fb->framebuffer_width;
	ssfn_dst.h = g__fb->framebuffer_height;
	ssfn_dst.p = g__fb->framebuffer_pitch;
	ssfn_dst.x = ssfn_dst.y = 0;
	ssfn_dst.fg = 0xFFFFFF;
	ssfn_dst.bg = 0x000000;

	KDEBUG(DEBUG_LEVEL_INFO, "graphic init done\n");
}

void vxPutc(char c, int x, int y, uint32_t fg, uint32_t bg) {
	ssfn_dst.fg = fg;
	ssfn_dst.bg = bg;
	ssfn_dst.x = x * 7;
	ssfn_dst.y = y * 15;
	ssfn_putc(c);
}

void put_pixel(int x, int y, uint32_t color) {
	pixel_t* pixel = (pixel_t*) ((uint8_t*) g__fb->framebuffer_addr
				     + y * g__fb->framebuffer_pitch + x * 4);
	pixel->r = color & 0xFF;
	pixel->g = (color >> 8) & 0xFF;
	pixel->b = (color >> 16) & 0xFF;
	pixel->a = (color >> 24) & 0xFF;
}

static inline uint8_t blend(uint8_t src, uint8_t dst, uint8_t a) {
	// Gunakan multiply+shift yang lebih akurat
	return ((src * a) + (dst * (255 - a)) + 128) >> 8;
	// +128 untuk rounding yang lebih baik
}

void put_pixel_alpha(int x, int y, pixel_t src) {
	// Bounds check
	if (x < 0 || y < 0 || x >= g__fb->framebuffer_width
	    || y >= g__fb->framebuffer_height)
		return;

	// Early exit untuk fully transparent
	if (src.a == 0)
		return;

	uint32_t* dst_ptr =
		(uint32_t*) ((uint8_t*) g__fb->framebuffer_addr
			     + y * g__fb->framebuffer_pitch + x * 4);

	// Fast path untuk fully opaque
	if (src.a == 255) {
		uint32_t r_val =
			(src.r * ((1 << g__fb->red_mask_size) - 1)) / 255;
		uint32_t g_val =
			(src.g * ((1 << g__fb->green_mask_size) - 1)) / 255;
		uint32_t b_val =
			(src.b * ((1 << g__fb->blue_mask_size) - 1)) / 255;

		*dst_ptr = (r_val << g__fb->red_mask_shift)
			   | (g_val << g__fb->green_mask_shift)
			   | (b_val << g__fb->blue_mask_shift);
		return;
	}

	// Alpha blending path
	uint32_t dst_color = *dst_ptr;

	// Extract langsung tanpa struct intermediate
	uint8_t dst_b = dst_color & 0xFF;
	uint8_t dst_g = (dst_color >> 8) & 0xFF;
	uint8_t dst_r = (dst_color >> 16) & 0xFF;

	// Blend
	uint8_t r = blend(src.r, dst_r, src.a);
	uint8_t g = blend(src.g, dst_g, src.a);
	uint8_t b = blend(src.b, dst_b, src.a);

	// Color packing - gunakan shift instead of division jika mask size
	// standard (8-bit)
	uint32_t r_val = (r * ((1 << g__fb->red_mask_size) - 1)) / 255;
	uint32_t g_val = (g * ((1 << g__fb->green_mask_size) - 1)) / 255;
	uint32_t b_val = (b * ((1 << g__fb->blue_mask_size) - 1)) / 255;

	*dst_ptr = (r_val << g__fb->red_mask_shift)
		   | (g_val << g__fb->green_mask_shift)
		   | (b_val << g__fb->blue_mask_shift);
}

void put_pixel_alpha_fast(int x, int y, pixel_t src) {
	if (x < 0 || y < 0 || x >= g__fb->framebuffer_width
	    || y >= g__fb->framebuffer_height)
		return;

	if (src.a == 0)
		return;

	uint32_t* dst_ptr =
		(uint32_t*) ((uint8_t*) g__fb->framebuffer_addr
			     + y * g__fb->framebuffer_pitch + x * 4);

	if (src.a == 255) {
		*dst_ptr = (src.r << 16) | (src.g << 8) | src.b;
		return;
	}

	uint32_t dst_color = *dst_ptr;
	uint32_t inv_a = 255 - src.a;

	// Blend semua channel sekaligus dengan SIMD-like operations
	uint32_t rb = (((src.r * src.a) + ((dst_color >> 16) & 0xFF) * inv_a)
		       & 0xFF00)
			      << 8
		      | (((src.b * src.a) + (dst_color & 0xFF) * inv_a) >> 8);
	uint32_t g = ((src.g * src.a) + (((dst_color >> 8) & 0xFF) * inv_a))
		     & 0xFF00;

	*dst_ptr = rb | g;
}