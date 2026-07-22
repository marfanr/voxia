#include "graphic.h"
#include "framebuffer.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "ioforge/ioforge.h"
#include "libk/debug/debug.h"
#include "memory/entry.h"
#include "memory/memory_utils.h"
#include "memory/vm_manager.h"
#include "spinlock.h"
#include "str.h"
#include "string.h"
#include "type.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <cpu/irq_lock.h>
#include <libk/serial.h>

// TODO: will be moved into console
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-warning-option"

/* SSFN Part*/
#define _STRING_H_

#define SSFN_memcmp memcmp

#define SSFN_memset ssfn_memset
static void* ssfn_memset(void* __s, int __c, size_t __n) {
	unsigned char* p = (unsigned char*)__s;
	while (__n--)
		*p++ = (unsigned char)__c;
	return __s;
}

#define SSFN_memcpy ssfn_memcpy
static void* ssfn_memcpy(void* __restrict__ __dest,
                         const void* __restrict__ __src, size_t __n) {
	unsigned char* d = (unsigned char*)__dest;
	const unsigned char* s = (const unsigned char*)__src;
	while (__n--)
		*d++ = *s++;
	return __dest;
}

#include <memory/kalloc.h>

static void* ssfn_realloc(void* ptr, size_t new_size) {
	if (new_size == 0) {
		if (ptr)
			kfree2(ptr);
		return NULL;
	}

	if (!ptr)
		return kalloc(new_size);

	kalloc_metadata_t* meta =
	    (kalloc_metadata_t*)((uintptr_t)ptr - sizeof(kalloc_metadata_t) -
	                         KALLOC_REDZONE_SIZE);

	size_t old_size = meta->size;
	void* new_ptr = kalloc(new_size);
	if (!new_ptr)
		return NULL;

	size_t copy = old_size < new_size ? old_size : new_size;
	ssfn_memcpy(new_ptr, ptr, copy);
	kfree2(ptr);

	return new_ptr;
}

static void ssfn_free_(void* ptr) {
	if (ptr)
		kfree2(ptr);
}

#define SSFN_realloc ssfn_realloc
#define SSFN_free ssfn_free_

#define SSFN_IMPLEMENTATION
#include <libk/ssfn.h>

#undef _STRING_H_
#pragma GCC diagnostic pop
/* SSFN Part End */

static volatile framebuffer_t* g__fb;

int g_font_size = 16;
static int g_font_baseline =
    14; /* default for unifont, updated after font load */
static uint8_t* g_font_buff = NULL;
static ssfn_buf_t dst;
static ssfn_t ssfn_ctx = {0};
static boolean_t ssfn_ready = false;
static spinlock_t gfx_lock = SPINLOCK_INIT;

static uint8_t* active_draw_buffer = NULL;
static int __graphic_last_id = 0;

/* FB OPS */
static vops_file_t __fb_ops = {0};
static int fb_read(vnode_t* vnode, void* buf, size_t len, size_t offset);
static long fb_write(vnode_t* vnode, void* buf, size_t len, size_t offset);

INIT(graphic) {
	g__fb = &ctx->framebuffer;
	LOG2_DEBUG("FB", "Fb addr %lx", g__fb->framebuffer_addr);

	if (g__fb->framebuffer_addr == 0)
		return;

	/* Load Main Font */
	dentry_ptr font_dentry;
	if (resolve_dentry("/init/fonts/unifont.sfn", 0, &font_dentry, 0) ==
	    VFS_OK) {
		g_font_buff = (uint8_t*)kalloc(font_dentry->vnode->size);
		LOG2_DEBUG("Graphic", "opened %s (%d kb) at 0x%lx",
		           font_dentry->name->c_str,
		           font_dentry->vnode->size / 1024, g_font_buff);

		memset(g_font_buff, 0, font_dentry->vnode->size);
		((vops_file_t*)font_dentry->vnode->ops)
		    ->read(font_dentry->vnode, g_font_buff,
		           font_dentry->vnode->size, 0);
	} else {
		LOG2_WARN("Graphic", "failed to load font");
	}

	/* Mapping Framebuffer */
	size_t fb_size = g__fb->framebuffer_pitch * g__fb->framebuffer_height;

	for (uint64_t i = 0; i < ctx->memory.memory_entries; i++) {
		memory_entry_t* entry = &ctx->memory.memory_map[i];
		if (entry->type == ENTRY_MMAP_FRAMEBUFFER) {
			fb_size = entry->length;
			auto size = entry->length / PAGE_SIZE_4KB;

			auto vaddr = vma_lookup_free_vaddr(
			    get_kernel_vmm_page(), VMA_REGION_A, size);
			paging_multiple_mmap(paging_get_highest_page_map(),
			                     vaddr, entry->base, size, 0b111);
			g__fb->framebuffer_addr = vaddr;
			break;
		}
	}
	serial2_printf("new framebuffer 0x%lx\n", g__fb->framebuffer_addr);

	// setup FB
	dentry_ptr fb_dentry;
	vxnamei("/dev/fb", &fb_dentry);
	auto fb_vnode = create_and_attach_vnode();
	fb_dentry->vnode = fb_vnode;
	fb_vnode->ops = &__fb_ops;
	fb_vnode->vnode_private = (void*)g__fb;
	fb_vnode->size = fb_size;

	__fb_ops.read = fb_read;
	__fb_ops.write = fb_write;

	// ssfn
	dst.ptr = (uint8_t*)g__fb->framebuffer_addr;
	active_draw_buffer = dst.ptr;
	dst.w = g__fb->framebuffer_width;
	dst.h = g__fb->framebuffer_height;
	dst.p = (uint16_t)g__fb->framebuffer_pitch;
	dst.x = 0;
	dst.y = 0;
	dst.fg = 0xFFFFFFFF;
	dst.bg = 0xFF000000;

	serial2_printf("dst: ptr=%lx w=%d h=%d p=%d\n", dst.ptr, dst.w, dst.h,
	               dst.p);

	if (g_font_buff) {
		ssfn_font_t* fhdr = (ssfn_font_t*)g_font_buff;
		int select_size = g_font_size;
		if (SSFN_TYPE_FAMILY(fhdr->type) != SSFN_FAMILY_MONOSPACE) {
			// Calculate the optimal font size to fit the cell
			// naturally without squishing
			select_size =
			    (g_font_size * fhdr->baseline) / fhdr->height;
			if (select_size < 8)
				select_size = 8;
			g_font_baseline = select_size;
		} else {
			// Monospace fonts natively fit the cell height
			g_font_baseline =
			    (g_font_size * fhdr->baseline + fhdr->height - 1) /
			    fhdr->height;
		}
		serial2_printf("font: height=%d baseline=%d type=%d, cell "
		               "size=%d selected size=%d baseline=%d\n",
		               fhdr->height, fhdr->baseline, fhdr->type,
		               g_font_size, select_size, g_font_baseline);

		int r = ssfn_load(&ssfn_ctx, g_font_buff);
		if (r != SSFN_OK) {
			LOG2_WARN("Graphic", "ssfn_load failed: %d", r);
			return;
		}

		ssfn_select(&ssfn_ctx, SSFN_FAMILY_ANY, NULL,
		            SSFN_STYLE_REGULAR | SSFN_STYLE_NODEFGLYPH,
		            select_size);
		ssfn_ready = true;
	}

	KDEBUG(DEBUG_LEVEL_INFO, "graphic init done\n");
}

/* Clear a character cell - must be called with gfx_lock held */
static void fill_cell_nolock(int px_x, int px_y, int cell_w, int cell_h,
                             uint32_t color) {
	int max_x = px_x + cell_w;
	int max_y = px_y + cell_h;
	if (px_x < 0)
		px_x = 0;
	if (px_y < 0)
		px_y = 0;
	if (max_x > (int)g__fb->framebuffer_width)
		max_x = g__fb->framebuffer_width;
	if (max_y > (int)g__fb->framebuffer_height)
		max_y = g__fb->framebuffer_height;

	for (int r = px_y; r < max_y; r++) {
		uint32_t* line =
		    (uint32_t*)(void*)((uint8_t*)active_draw_buffer +
		                       r * g__fb->framebuffer_pitch);
		for (int c = px_x; c < max_x; c++) {
			line[c] = color;
		}
	}
}

void putc_nolock(char c, int col, int row, uint32_t fg, uint32_t bg) {
	if (!ssfn_ready || !active_draw_buffer)
		return;

	dst.ptr = active_draw_buffer;
	dst.fg = fg | 0xFF000000;
	dst.bg = 0;
	dst.x = col * (g_font_size / 2);
	dst.y = row * g_font_size + g_font_baseline;

	fill_cell_nolock(col * (g_font_size / 2), row * g_font_size,
	                 g_font_size / 2, g_font_size, bg | 0xFF000000);

	char str[2] = {c, '\0'};
	int r = ssfn_render(&ssfn_ctx, &dst, str);
	if (r < 0)
		serial2_printf("ssfn_render err: %d\n", r);
}

void putc(char c, int col, int row, uint32_t fg, uint32_t bg) {
	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);
	putc_nolock(c, col, row, fg, bg);
	spin_release(&gfx_lock);
	irq_restore(flags);
}

void putc_utf8_nolock(const char* s, int col, int row, uint32_t fg,
                      uint32_t bg) {
	if (!ssfn_ready || !active_draw_buffer)
		return;

	dst.ptr = active_draw_buffer;
	dst.fg = fg | 0xFF000000;
	dst.bg = 0;
	dst.x = col * (g_font_size / 2);
	dst.y = row * g_font_size + g_font_baseline;

	fill_cell_nolock(col * (g_font_size / 2), row * g_font_size,
	                 g_font_size / 2, g_font_size, bg | 0xFF000000);

	int r = ssfn_render(&ssfn_ctx, &dst, s);
	if (r < 0)
		serial2_printf("ssfn_render err: %d\n", r);
}

void putc_utf8(const char* s, int col, int row, uint32_t fg, uint32_t bg) {
	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);
	putc_utf8_nolock(s, col, row, fg, bg);
	spin_release(&gfx_lock);
	irq_restore(flags);
}

int utf8_char_len(uint8_t c) {
	if ((c & 0x80) == 0)
		return 1;
	if ((c & 0xE0) == 0xC0)
		return 2;
	if ((c & 0xF0) == 0xE0)
		return 3;
	if ((c & 0xF8) == 0xF0)
		return 4;
	return 1;
}

void put_pixel(int x, int y, uint32_t color) {
	pixel_t* pixel = (pixel_t*)((uint8_t*)g__fb->framebuffer_addr +
	                            y * g__fb->framebuffer_pitch + x * 4);
	pixel->r = color & 0xFF;
	pixel->g = (color >> 8) & 0xFF;
	pixel->b = (color >> 16) & 0xFF;
	pixel->a = (color >> 24) & 0xFF;
}

static inline uint8_t blend(uint8_t src, uint8_t d, uint8_t a) {
	return ((src * a) + (d * (255 - a)) + 128) >> 8;
}

void put_pixel_alpha(int x, int y, pixel_t src) {
	// Bounds check
	if (x < 0 || y < 0 || x >= g__fb->framebuffer_width ||
	    y >= g__fb->framebuffer_height)
		return;

	// Early exit untuk fully transparent
	if (src.a == 0)
		return;

	uint32_t* dst_ptr = (uint32_t*)PTR_ADD(
	    g__fb->framebuffer_addr, y * g__fb->framebuffer_pitch + x * 4);

	// Fast path untuk fully opaque
	if (src.a == 255) {
		uint32_t r_val =
		    (src.r * ((1 << g__fb->red_mask_size) - 1)) / 255;
		uint32_t g_val =
		    (src.g * ((1 << g__fb->green_mask_size) - 1)) / 255;
		uint32_t b_val =
		    (src.b * ((1 << g__fb->blue_mask_size) - 1)) / 255;

		*dst_ptr = (r_val << g__fb->red_mask_shift) |
		           (g_val << g__fb->green_mask_shift) |
		           (b_val << g__fb->blue_mask_shift);
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

	*dst_ptr = (r_val << g__fb->red_mask_shift) |
	           (g_val << g__fb->green_mask_shift) |
	           (b_val << g__fb->blue_mask_shift);
}

void put_pixel_alpha_fast(int x, int y, pixel_t src) {
	if (x < 0 || y < 0 || x >= g__fb->framebuffer_width ||
	    y >= g__fb->framebuffer_height)
		return;

	if (src.a == 0)
		return;

	uint32_t* dst_ptr =
	    (uint32_t*)(void*)(((uint8_t*)g__fb->framebuffer_addr +
	                        y * g__fb->framebuffer_pitch + x * 4));

	if (src.a == 255) {
		*dst_ptr = (uint32_t)((src.r << 16) | (src.g << 8) | src.b);
		return;
	}

	uint32_t dst_color = *dst_ptr;
	uint32_t inv_a = 255 - src.a;

	// Blend semua channel sekaligus dengan SIMD-like operations
	uint32_t rb =
	    (((src.r * src.a) + ((dst_color >> 16) & 0xFF) * inv_a) & 0xFF00)
	        << 8 |
	    (((src.b * src.a) + (dst_color & 0xFF) * inv_a) >> 8);
	uint32_t g =
	    ((src.g * src.a) + (((dst_color >> 8) & 0xFF) * inv_a)) & 0xFF00;

	*dst_ptr = rb | g;
}

void clear_screen(uint32_t color) {
	if (!active_draw_buffer)
		return;
	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);
	memset((void*)active_draw_buffer, color,
	       g__fb->framebuffer_pitch * g__fb->framebuffer_height);
	spin_release(&gfx_lock);
	irq_restore(flags);
}

uint32_t vxGetWidth(void) { return dst.w; }
uint32_t vxGetHeight(void) { return dst.h; }

void vxScroll_nolock(int px, uint32_t bg_color) {
	uint32_t row_bytes = dst.p;
	uint32_t total = row_bytes * (dst.h - px);
	memmove(dst.ptr, dst.ptr + row_bytes * px, total);
	fill_rect_nolock(0, (int)(dst.h - px), (int)dst.w, px, bg_color);
}

void vxScroll(int px, uint32_t bg_color) {
	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);
	vxScroll_nolock(px, bg_color);
	spin_release(&gfx_lock);
	irq_restore(flags);
}

void vxScrollDown_nolock(int px, uint32_t bg_color) {
	uint32_t row_bytes = dst.p;
	uint32_t total = row_bytes * (dst.h - px);
	memmove(dst.ptr + row_bytes * px, dst.ptr, total);
	fill_rect_nolock(0, 0, (int)dst.w, px, bg_color);
}

void vxScrollDown(int px, uint32_t bg_color) {
	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);
	vxScrollDown_nolock(px, bg_color);
	spin_release(&gfx_lock);
	irq_restore(flags);
}

void vxScrollRegion_nolock(int px, int top_row, int bottom_row,
                           uint32_t bg_color) {
	uint32_t top_y = (uint32_t)top_row * (uint32_t)px;
	uint32_t bot_y = ((uint32_t)bottom_row + 1) * (uint32_t)px;
	uint32_t row_bytes = dst.p;
	uint32_t total = row_bytes * (bot_y - top_y - (uint32_t)px);
	memmove(dst.ptr + top_y * row_bytes,
	        dst.ptr + (top_y + (uint32_t)px) * row_bytes, total);
	fill_rect_nolock(0, (int)(bot_y - (uint32_t)px), (int)dst.w, px,
	                 bg_color);
}

void vxScrollRegion(int px, int top_row, int bottom_row, uint32_t bg_color) {
	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);
	vxScrollRegion_nolock(px, top_row, bottom_row, bg_color);
	spin_release(&gfx_lock);
	irq_restore(flags);
}

void vxScrollDownRegion_nolock(int px, int top_row, int bottom_row,
                               uint32_t bg_color) {
	uint32_t top_y = (uint32_t)top_row * (uint32_t)px;
	uint32_t bot_y = ((uint32_t)bottom_row + 1) * (uint32_t)px;
	uint32_t row_bytes = dst.p;
	uint32_t total = row_bytes * (bot_y - top_y - (uint32_t)px);
	memmove(dst.ptr + (top_y + (uint32_t)px) * row_bytes,
	        dst.ptr + top_y * row_bytes, total);
	fill_rect_nolock(0, (int)top_y, (int)dst.w, px, bg_color);
}

void vxScrollDownRegion(int px, int top_row, int bottom_row,
                        uint32_t bg_color) {
	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);
	vxScrollDownRegion_nolock(px, top_row, bottom_row, bg_color);
	spin_release(&gfx_lock);
	irq_restore(flags);
}

uint32_t screen_cols(void) {
	uint32_t w = vxGetWidth();
	if (w == 0)
		return 80;
	return (w / (g_font_size / 2));
}
uint32_t screen_rows(void) {
	uint32_t h = vxGetHeight();
	if (h == 0)
		return 25;
	return (h / g_font_size);
}

void fill_rect_nolock(int x, int y, int w, int h, uint32_t color) {
	if (!active_draw_buffer || !g__fb)
		return;

	int max_y = y + h;
	int max_x = x + w;
	if (y < 0)
		y = 0;
	if (x < 0)
		x = 0;
	if (max_y > (int)g__fb->framebuffer_height)
		max_y = g__fb->framebuffer_height;
	if (max_x > (int)g__fb->framebuffer_width)
		max_x = g__fb->framebuffer_width;

	// Menghitung berapa byte (atau piksel) yang diisi per baris
	int draw_width = max_x - x;
	if (draw_width <= 0)
		return;

	for (int row = y; row < max_y; row++) {
		uint32_t* line =
		    (uint32_t*)(void*)((uint8_t*)active_draw_buffer +
		                       row * g__fb->framebuffer_pitch);

		// Geser pointer memori dari sisi kiri ke offset X
		uint32_t* dest = &line[x];

		for (int col = 0; col < draw_width; col++) {
			dest[col] = color;
		}
	}
}

uint8_t* graphic_alloc_backbuffer(void) {
	if (!g__fb || g__fb->framebuffer_addr == 0)
		return NULL;
	size_t size = g__fb->framebuffer_pitch * g__fb->framebuffer_height;
	uint8_t* buf = (uint8_t*)kalloc(size);
	if (buf) {
		memset(buf, 0, size);
	}
	return buf;
}

void graphic_free_backbuffer(uint8_t* buffer) {
	if (buffer) {
		kfree2(buffer);
	}
}

void graphic_set_draw_buffer(uint8_t* buffer) {
	if (buffer == NULL) {
		active_draw_buffer = (uint8_t*)g__fb->framebuffer_addr;
	} else {
		active_draw_buffer = buffer;
	}
	dst.ptr = active_draw_buffer;
}

static inline void fast_reps_memcopy(void* dest, const void* src, size_t len) {
	size_t qwords = len / 8;
	size_t bytes = len % 8;
	asm volatile("rep movsq\n\t"
	             "mov %3, %%rcx\n\t"
	             "rep movsb\n\t"
	             : "+D"(dest), "+S"(src), "+c"(qwords)
	             : "r"(bytes)
	             : "memory");
}

void graphic_flush_backbuffer_nolock(const uint8_t* backbuffer) {
	if (!backbuffer || !g__fb || g__fb->framebuffer_addr == 0)
		return;

	fast_reps_memcopy((void*)g__fb->framebuffer_addr, (void*)backbuffer,
	                  g__fb->framebuffer_pitch * g__fb->framebuffer_height);
}

void graphic_flush_backbuffer(const uint8_t* backbuffer) {
	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);
	graphic_flush_backbuffer_nolock(backbuffer);
	spin_release(&gfx_lock);
	irq_restore(flags);
}

void graphic_flush_backbuffer_rows_nolock(const uint8_t* backbuffer,
                                          int start_row, int end_row) {
	if (!backbuffer || !g__fb || g__fb->framebuffer_addr == 0)
		return;
	if (start_row > end_row)
		return;

	int start_y = start_row * g_font_size;
	int end_y = (end_row + 1) * g_font_size;
	if (start_y < 0)
		start_y = 0;
	if (end_y > (int)g__fb->framebuffer_height)
		end_y = g__fb->framebuffer_height;

	int pitch = g__fb->framebuffer_pitch;
	uint8_t* dest = (uint8_t*)g__fb->framebuffer_addr + start_y * pitch;
	const uint8_t* src = backbuffer + start_y * pitch;
	size_t len = (size_t)(end_y - start_y) * pitch;

	if (len > 0) {
		fast_reps_memcopy(dest, (void*)src, len);
	}
}

void graphic_flush_backbuffer_rows(const uint8_t* backbuffer, int start_row,
                                   int end_row) {
	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);
	graphic_flush_backbuffer_rows_nolock(backbuffer, start_row, end_row);
	spin_release(&gfx_lock);
	irq_restore(flags);
}

void graphic_lock(void) { spin_acquire(&gfx_lock); }

void graphic_unlock(void) { spin_release(&gfx_lock); }

bool graphic_is_locked(void) { return spin_is_locked(&gfx_lock); }

void graphic_set_font_size(int size) {
	if (!ssfn_ready || !g_font_buff)
		return;

	if (size < 8)
		size = 8;
	if (size > 192)
		size = 192;

	uintptr_t flags = irq_save();
	spin_acquire(&gfx_lock);

	ssfn_font_t* fhdr = (ssfn_font_t*)g_font_buff;
	g_font_size = size;
	int select_size = size;
	if (SSFN_TYPE_FAMILY(fhdr->type) != SSFN_FAMILY_MONOSPACE) {
		select_size = (size * fhdr->baseline) / fhdr->height;
		if (select_size < 8)
			select_size = 8;
		g_font_baseline = select_size;
	} else {
		g_font_baseline =
		    (size * fhdr->baseline + fhdr->height - 1) / fhdr->height;
	}

	ssfn_select(&ssfn_ctx, SSFN_FAMILY_ANY, NULL,
	            SSFN_STYLE_REGULAR | SSFN_STYLE_NODEFGLYPH, select_size);

	spin_release(&gfx_lock);
	irq_restore(flags);
}

/* FB API*/
static int fb_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	auto fb = (framebuffer_t*)vnode->vnode_private;
	memcopy(buf, (void*)(fb->framebuffer_addr + offset), len);
	return (int)len;
}
static long fb_write(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	auto fb = (framebuffer_t*)vnode->vnode_private;

	if ((offset + len) > vnode->size)
		len = vnode->size - offset;

	memcopy((void*)(fb->framebuffer_addr + offset), buf, len);
	return len;
}

/* GRAPHIC API */
static struct graphic_device* __graphic_device_list = 0;

static struct graphic_context* find_context(struct graphic_device* g,
                                            uint32_t id) {
	struct graphic_context* cur = g->context_list;
	while (cur) {
		if (cur->id == id)
			return cur;
		cur = cur->next;
	}
	return NULL;
}

static struct graphic_resource* find_resource(struct graphic_device* g,
                                              uint32_t id) {
	struct graphic_resource* cur = g->resource_list;
	while (cur) {
		if (cur->id == id)
			return cur;
		cur = cur->next;
	}
	return NULL;
}

int graphic_ioctl(vnode_t* vnode, uint32_t request, void* argp) {
	auto g = (struct graphic_device*)vnode->vnode_private;
	if (!g) {
		serial2_printf("graphic_ioctl: device is NULL\n");
		return -1;
	}
	if (!argp) {
		serial2_printf("graphic_ioctl: invalid command\n");
		return -1;
	}

	switch (request) {
	case GRAPHIC_IOCTL_CREATE_RESOURCE: {
		struct graphic_ioctl_create_resource_cmd* cmd =
		    (struct graphic_ioctl_create_resource_cmd*)argp;

		auto des = &cmd->desc;
		des->ctx = find_context(g, des->ctx_id);
		if (!des->ctx)
			return -1;
		
		static uint32_t global_res_id = 100;
		des->id = __atomic_add_fetch(&global_res_id, 1, __ATOMIC_SEQ_CST);

		struct graphic_resource* res = NULL;
		if (!g->ops->resource_create)
			return -1;

		int ret = g->ops->resource_create(g, des, &res);
		if (ret == 0 && res) {
			res->next = g->resource_list;
			g->resource_list = res;
			cmd->resource_id = res->id;
		}
		return ret;
	}
	case GRAPHIC_IOCTL_CREATE_CONTEXT: {
		struct graphic_ioctl_create_context_cmd* cmd =
		    (struct graphic_ioctl_create_context_cmd*)argp;

		auto des = &cmd->desc;
		static uint32_t global_ctx_id = 10;
		des->id = __atomic_add_fetch(&global_ctx_id, 1, __ATOMIC_SEQ_CST);

		struct graphic_context* res = NULL;
		if (!g->ops->create_context)
			return -1;

		int ret = g->ops->create_context(g, des, &res);
		if (ret == 0 && res) {
			res->next = g->context_list;
			g->context_list = res;
			cmd->context_id = res->id;
		}
		return ret;
	}
	case GRAPHIC_IOCTL_ATTACH_BACKING: {
		struct graphic_ioctl_attach_backing_cmd* cmd =
		    (struct graphic_ioctl_attach_backing_cmd*)argp;

		auto res = find_resource(g, cmd->resource_id);
		if (!res)
			return -1;

		if (!g->ops->resource_attach_backing)
			return -1;

		return g->ops->resource_attach_backing(g, res);
	}
	case GRAPHIC_IOCTL_DESTROY_RESOURCE: {
		struct destroy_resource_cmd {
			uint32_t resource_id;
		};
		struct destroy_resource_cmd* cmd =
		    (struct destroy_resource_cmd*)argp;

		auto res = find_resource(g, cmd->resource_id);
		if (!res)
			return -1;

		if (!g->ops || !g->ops->resource_destroy)
			return -1;

		/* Remove from resource list */
		if (g->resource_list == res) {
			g->resource_list = res->next;
		} else {
			for (auto cur = &g->resource_list; *cur;
			     cur = &(*cur)->next) {
				if ((*cur)->next == res) {
					(*cur)->next = res->next;
					break;
				}
			}
		}

		int ret = g->ops->resource_destroy(g, res);
		return ret;
	}
	case GRAPHIC_IOCTL_DESTROY_CONTEXT: {
		struct destroy_context_cmd {
			uint32_t context_id;
		};
		struct destroy_context_cmd* cmd =
		    (struct destroy_context_cmd*)argp;

		auto ctx = find_context(g, cmd->context_id);
		if (!ctx)
			return -1;

		if (!g->ops || !g->ops->destroy_context)
			return -1;

		/* Remove from context list */
		if (g->context_list == ctx) {
			g->context_list = ctx->next;
		} else {
			for (auto cur = &g->context_list; *cur;
			     cur = &(*cur)->next) {
				if ((*cur)->next == ctx) {
					(*cur)->next = ctx->next;
					break;
				}
			}
		}

		return g->ops->destroy_context(g, ctx);
	}
	case GRAPHIC_IOCTL_BIND_RESOURCE: {
		struct graphic_ioctl_bind_resource_cmd* cmd =
		    (struct graphic_ioctl_bind_resource_cmd*)argp;

		auto ctx = find_context(g, cmd->context_id);
		auto res = find_resource(g, cmd->resource_id);

		if (!ctx || !res || !ctx->ops || !ctx->ops->bind_resource)
			return -1;

		return ctx->ops->bind_resource(ctx, res);
	}
	case GRAPHIC_IOCTL_TRANSFER: {
		struct graphic_transfer_cmd* cmd =
		    (struct graphic_transfer_cmd*)argp;
		auto ctx = find_context(g, cmd->context_id);
		auto res = find_resource(g, cmd->resource_id);
		if (!ctx || !res || !ctx->ops || !ctx->ops->transfer)
			return -1;

		if (cmd->data && res->vaddr) {
			uint32_t bpp = 4;
			switch (res->format) {
			case GRAPHIC_FORMAT_RGB565:
			case GRAPHIC_FORMAT_Z16_UNORM:
			case GRAPHIC_FORMAT_L16_UNORM:
			case GRAPHIC_FORMAT_L8A8_UNORM:
			case GRAPHIC_FORMAT_UYVY:
			case GRAPHIC_FORMAT_YUYV:
				bpp = 2;
				break;
			case GRAPHIC_FORMAT_L8_UNORM:
			case GRAPHIC_FORMAT_A8_UNORM:
			case GRAPHIC_FORMAT_I8_UNORM:
			case GRAPHIC_FORMAT_NONE:
				bpp = 1;
				break;
			default:
				bpp = 4;
				break;
			}

			uint32_t dst_stride = res->width * bpp;
			uint32_t src_stride = cmd->w * bpp;

			// Set the offset for QEMU so it reads from the correct
			// sub-rectangle
			if (cmd->offset == 0) {
				cmd->offset =
				    (cmd->y * dst_stride) + (cmd->x * bpp);
			}

			uint8_t* dst = (uint8_t*)res->vaddr + cmd->offset;
			uint8_t* src = (uint8_t*)cmd->data;

			for (uint32_t i = 0; i < cmd->h; i++) {
				memcopy(dst + i * dst_stride,
				        src + i * src_stride, src_stride);
			}
		}

		return ctx->ops->transfer(ctx, res, cmd);
	}
	case GRAPHIC_IOCTL_TRANSFER2: {
		struct graphic_transfer2_cmd* cmd =
		    (struct graphic_transfer2_cmd*)argp;
		auto ctx = find_context(g, cmd->context_id);
		auto res = find_resource(g, cmd->resource_id);
		if (!ctx || !res || !ctx->ops || !ctx->ops->transfer2)
			return -1;

		// we don't have data field in transfer2 yet, wait
		return ctx->ops->transfer2(ctx, res, cmd);
	}
	case GRAPHIC_IOCTL_TRANSFER_FROM: {
		struct graphic_transfer_cmd* cmd =
		    (struct graphic_transfer_cmd*)argp;
		auto ctx = find_context(g, cmd->context_id);
		auto res = find_resource(g, cmd->resource_id);
		if (!ctx || !res || !ctx->ops || !ctx->ops->transfer_from)
			return -1;

		uint32_t bpp = 4;
		switch (res->format) {
		case GRAPHIC_FORMAT_RGB565:
		case GRAPHIC_FORMAT_Z16_UNORM:
		case GRAPHIC_FORMAT_L16_UNORM:
		case GRAPHIC_FORMAT_L8A8_UNORM:
		case GRAPHIC_FORMAT_UYVY:
		case GRAPHIC_FORMAT_YUYV:
			bpp = 2;
			break;
		case GRAPHIC_FORMAT_L8_UNORM:
		case GRAPHIC_FORMAT_A8_UNORM:
		case GRAPHIC_FORMAT_I8_UNORM:
		case GRAPHIC_FORMAT_NONE:
			bpp = 1;
			break;
		default:
			bpp = 4;
			break;
		}

		uint32_t src_stride = res->width * bpp;
		uint32_t dst_stride = cmd->w * bpp;

		if (cmd->offset == 0) {
			cmd->offset = (cmd->y * src_stride) + (cmd->x * bpp);
		}

		int ret = ctx->ops->transfer_from(ctx, res, cmd);
		if (ret == 0 && cmd->data && res->vaddr) {
			uint8_t* src = (uint8_t*)res->vaddr + cmd->offset;
			uint8_t* _dst = (uint8_t*)cmd->data;

			for (uint32_t i = 0; i < cmd->h; i++) {
				memcopy(_dst + i * dst_stride,
				        src + i * src_stride, dst_stride);
			}
		}
		return ret;
	}
	case GRAPHIC_IOCTL_SUBMIT: {
		struct graphic_ioctl_submit_cmd* cmd =
		    (struct graphic_ioctl_submit_cmd*)argp;
		auto ctx = find_context(g, cmd->context_id);
		if (!ctx || !ctx->ops || !ctx->ops->submit)
			return -1;
		return ctx->ops->submit(ctx, cmd->commands, cmd->size);
	}
	case GRAPHIC_IOCTL_SET_SCANOUT: {
		struct graphic_ioctl_set_scanout_cmd* cmd =
		    (struct graphic_ioctl_set_scanout_cmd*)argp;
		struct graphic_scanout* scanout = NULL;
		for (struct graphic_scanout* s = g->scanout_list; s;
		     s = s->next) {
			if (s->id == cmd->scanout_id) {
				scanout = s;
				break;
			}
		}
		auto res = find_resource(g, cmd->resource_id);
		if (!scanout || !res || !g->ops || !g->ops->scanout_set)
			return -1;
		return g->ops->scanout_set(g, scanout, res);
	}
	case GRAPHIC_IOCTL_RESOURCE_FLUSH: {
		struct graphic_ioctl_resource_flush_cmd* cmd =
		    (struct graphic_ioctl_resource_flush_cmd*)argp;
		auto res = find_resource(g, cmd->resource_id);
		if (!res || !g->ops || !g->ops->resource_flush)
			return -1;
		return g->ops->resource_flush(g, res, cmd->x, cmd->y,
		                              cmd->width, cmd->height);
	}
	case GRAPHIC_IOCTL_UPDATE_CURSOR: {
		struct graphic_ioctl_update_cursor_cmd* cmd =
		    (struct graphic_ioctl_update_cursor_cmd*)argp;
		struct graphic_scanout* scanout = NULL;
		for (struct graphic_scanout* s = g->scanout_list; s;
		     s = s->next) {
			if (s->id == cmd->scanout_id) {
				scanout = s;
				break;
			}
		}
		auto res = find_resource(g, cmd->resource_id);
		if (!scanout || !res || !g->ops || !g->ops->update_cursor)
			return -1;
		return g->ops->update_cursor(g, scanout, res, cmd->hot_x,
		                             cmd->hot_y);
	}
	case GRAPHIC_IOCTL_MOVE_CURSOR: {
		struct graphic_ioctl_move_cursor_cmd* cmd =
		    (struct graphic_ioctl_move_cursor_cmd*)argp;
		struct graphic_scanout* scanout = NULL;
		for (struct graphic_scanout* s = g->scanout_list; s;
		     s = s->next) {
			if (s->id == cmd->scanout_id) {
				scanout = s;
				break;
			}
		}
		if (!scanout || !g->ops || !g->ops->move_cursor)
			return -1;
		return g->ops->move_cursor(g, scanout, cmd->x, cmd->y);
	}
	}

	serial2_printf("graphic_ioctl: unknown request %d\n", request);
	return -1;
}

static vops_file_t __graphic_dev_ops = {
    .ioctl = graphic_ioctl,
};

KERNEL_API
struct graphic_device* create_graphic_device() {
	auto g = (struct graphic_device*)kalloc(sizeof(struct graphic_device));
	memset(g, 0, sizeof(struct graphic_device));

	g->id = __graphic_last_id++;

	auto cur = &__graphic_device_list;
	while (*cur)
		cur = &(*cur)->next;
	*cur = g;

	// create dri
	dentry_ptr dri_dentry;
	auto dri = str("/dev/dri/card");
	auto current_dri = str_concat(dri, itoa(0, 10));
	vxnamei(current_dri->c_str, &dri_dentry);
	str_release(current_dri);
	str_release(dri);

	auto dri_vnode = create_and_attach_vnode();
	dri_dentry->vnode = dri_vnode;
	dri_vnode->type = VNODE_TYPE_DEV;
	dri_vnode->permission = 644;
	dri_vnode->ops = &__graphic_dev_ops;
	dri_vnode->vnode_private = g;

	return g;
}

KERNEL_API
struct graphic_scanout* graphic_alloc_scanout(int id,
                                              struct graphic_device* device,
                                              int width, int height, int x,
                                              int y) {
	auto g =
	    (struct graphic_scanout*)kalloc(sizeof(struct graphic_scanout));
	memset(g, 0, sizeof(struct graphic_scanout));

	g->id = id;
	g->width = width;
	g->height = height;
	g->x = x;
	g->y = y;
	device->scanout_count++;

	g->next = NULL;

	auto cur = &device->scanout_list;
	while (*cur)
		cur = &(*cur)->next;
	*cur = g;

	return g;
}

KERNEL_API
struct graphic_device* graphic_get_device(uint16_t id) {
	auto cur = &__graphic_device_list;
	while (*cur) {
		if ((*cur)->id == id)
			return *cur;
		cur = &(*cur)->next;
	}
	return NULL;
}
