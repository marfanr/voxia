#include "graphic.h"
#include "framebuffer.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "memory/entry.h"
#include "memory/memory_utils.h"
#include "memory/vm_manager.h"
#include "str.h"
#include "vfs/enum.h"
#include <libk/serial.h>
// HAPUS baris ini dari atas — sudah ada di bawah sebelum #include ssfn.h
// #define SSFN_IMPLEMENTATION   ← HAPUS yang ini (duplikat)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-warning-option"

#define _STRING_H_

#define SSFN_memcmp memcmp
static int memcmp(const void* __s1, const void* __s2, size_t __n) {
	const unsigned char* a = (const unsigned char*) __s1;
	const unsigned char* b = (const unsigned char*) __s2;
	while (__n--) {
		if (*a != *b)
			return (int) *a - (int) *b;
		a++;
		b++;
	}
	return 0;
}

#define SSFN_memset ssfn_memset
static void* ssfn_memset(void* __s, int __c, size_t __n) {
	unsigned char* p = (unsigned char*) __s;
	while (__n--)
		*p++ = (unsigned char) __c;
	return __s;
}

#define SSFN_memcpy ssfn_memcpy
static void* ssfn_memcpy(void* __restrict__ __dest,
			 const void* __restrict__ __src, size_t __n) {
	unsigned char* d = (unsigned char*) __dest;
	const unsigned char* s = (const unsigned char*) __src;
	while (__n--)
		*d++ = *s++;
	return __dest;
}

#include <memory/kalloc.h>

static void* ssfn_realloc(void* ptr, size_t new_size) {
	size_t* blk;
	size_t old_size = 0;

	if (new_size == 0) {
		if (ptr) {
			blk = (size_t*) ptr - 1;
			kfree(blk, blk[0] + sizeof(size_t));
		}
		return NULL;
	}

	blk = (size_t*) kalloc(new_size + sizeof(size_t));
	if (!blk)
		return NULL;
	blk[0] = new_size;

	if (ptr) {
		size_t* old_blk = (size_t*) ptr - 1;
		old_size = old_blk[0];
		size_t copy = old_size < new_size ? old_size : new_size;
		ssfn_memcpy(blk + 1, ptr, copy);
		kfree(old_blk, old_size + sizeof(size_t));
	}

	return blk + 1;
}

static void ssfn_free_(void* ptr) {
	if (!ptr)
		return;
	size_t* blk = (size_t*) ptr - 1; /* ← fix: pakai header size */
	kfree(blk, blk[0] + sizeof(size_t));
}

#define SSFN_realloc ssfn_realloc
#define SSFN_free ssfn_free_

#define SSFN_IMPLEMENTATION
#include <libk/ssfn.h>

#undef _STRING_H_
#pragma GCC diagnostic pop

#define FONT_SIZE 15

volatile framebuffer_t* g__fb;
static ssfn_buf_t dst;
static ssfn_t ssfn_ctx = {0};

INIT(graphic) {
	g__fb = &ctx->framebuffer;
	LOG2_DEBUG("FB", "Fb addr %p", g__fb->framebuffer_addr);

	if (g__fb->framebuffer_addr == 0)
		return;

	/* ── load font ──────────────────────────────────────────────────────── */
	dentry_ptr font_dentry;
	uint8_t* font_buff = 0;
	if (vxResolveDentry("/init/fonts/unifont.sfn", 0, &font_dentry, 0)
	    == VFS_OK) {
		LOG2_DEBUG("Graphic", "opened %s (%d kb)",
			   font_dentry->name->c_str,
			   font_dentry->vnode->size / 1024);
		font_buff = (uint8_t*) kalloc(font_dentry->vnode->size);
		((vops_file_t*) font_dentry->vnode->ops)
			->read(font_dentry->vnode, font_buff,
			       font_dentry->vnode->size, 0);
	} else {
		LOG2_WARN("Graphic", "failed to load font");
	}

	/* ── remap framebuffer ke virtual address ───────────────────────────── */
	for (uint64_t i = 0; i < ctx->memory.memory_entries; i++) {
		memory_entry_t* entry = &ctx->memory.memory_map[i];
		if (entry->type == ENTRY_MMAP_FRAMEBUFFER) {
			vxMultipleMmap(paging_get_highest_page_map(),
				       0xFFFFFA0000000000, entry->base,
				       entry->length / PAGE_SIZE, 0b111);
			vma_register(entry->base, 0xFFFFFA0000000000,
				     entry->length / PAGE_SIZE);
			g__fb->framebuffer_addr = 0xFFFFFA0000000000;
			break;
		}
	}

	serial2_printf("new framebuffer 0x%x\n", g__fb->framebuffer_addr);

	/* ── setup dst buffer ───────────────────────────────────────────────── */
	dst.ptr = (uint8_t*) g__fb->framebuffer_addr;
	dst.w = g__fb->framebuffer_width;
	dst.h = g__fb->framebuffer_height;
	dst.p = g__fb->framebuffer_pitch;
	dst.x = 0;
	dst.y = 0;
	dst.fg = 0xFFFFFFFF; /* ARGB: AA RR GG BB — putih opak              */
	dst.bg = 0xFF000000; /* ARGB: hitam opak                             */

	/* ── init SSFN ──────────────────────────────────────────────────────── */
	if (font_buff) {
		int r = ssfn_load(&ssfn_ctx, font_buff);
		if (r != SSFN_OK) {
			LOG2_WARN("Graphic", "ssfn_load failed: %d", r);
			return;
		}

		ssfn_select(&ssfn_ctx, SSFN_FAMILY_ANY, NULL, /* family */
			    SSFN_STYLE_REGULAR,		      /* style */
			    FONT_SIZE			      /* size */
		);
	}

	KDEBUG(DEBUG_LEVEL_INFO, "graphic init done\n");
}

void vxPutc(char c, int col, int row, uint32_t fg, uint32_t bg) {
	dst.fg = fg | 0xFF000000; /* pastikan alpha selalu opak               */
	dst.bg = bg | 0xFF000000;
	dst.x = col * (FONT_SIZE / 2);
	dst.y = 20 + row * FONT_SIZE;

	char str[2] = {c, '\0'};
	int r = ssfn_render(&ssfn_ctx, &dst, str);
	if (r < 0)
		serial2_printf("ssfn_render err: %d\n", r);
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

	uint32_t* dst_ptr = (uint32_t*) PTR_ADD(
		g__fb->framebuffer_addr, y * g__fb->framebuffer_pitch + x * 4);

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
		(uint32_t*) (void*) (((uint8_t*) g__fb->framebuffer_addr
				      + y * g__fb->framebuffer_pitch + x * 4));

	if (src.a == 255) {
		*dst_ptr = (uint32_t) ((src.r << 16) | (src.g << 8) | src.b);
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

// static void clear_screen(uint32_t color) {
// 	for (int y = 0; y < g__fb->framebuffer_height; y++) {
// 		for (int x = 0; x < g__fb->framebuffer_width; x++) {
// 			put_pixel(x, y, color);
// 		}
// 	}
// }

__attribute__((unused)) static void scroll_up(int lines, uint32_t bg_color) {
	int line_height = 15; // Asumsi tinggi karakter 15px
	int scroll_amount = lines * line_height;

	// Geser framebuffer ke atas
	memcopy((void*) g__fb->framebuffer_addr,
		(void*) (g__fb->framebuffer_addr
			 + (uint64_t) scroll_amount * g__fb->framebuffer_pitch),
		(size_t) ((g__fb->framebuffer_height - lines)
			  * g__fb->framebuffer_pitch));

	// Bersihkan area kosong di bawah
	for (int y = g__fb->framebuffer_height - scroll_amount;
	     y < g__fb->framebuffer_height; y++) {
		for (int x = 0; x < g__fb->framebuffer_width; x++) {
			put_pixel(x, y, bg_color);
		}
	}
}