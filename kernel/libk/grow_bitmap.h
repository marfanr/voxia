#ifndef __LIBK_GROW_BITMAP_H__
#define __LIBK_GROW_BITMAP_H__

#include <memory/phys_base_allocator.h>
#include <memory/phys_window.h>
#include <type.h>

#define GROW_BITMAP_BLOCK_PAGES 32768 // 128 MB per block
#define GROW_BITMAP_WORDS_PER_BLOCK (GROW_BITMAP_BLOCK_PAGES / 64)
// TODO: move this into Kconfig
#define GROW_BITMAP_ABSOLUTE_MAX_BLOCKS 256 // Hard limit array size (Max 32 GB)

typedef struct {
	uintptr_t base_vaddr;
	size_t page_size;
	uintptr_t blocks_phys[GROW_BITMAP_ABSOLUTE_MAX_BLOCKS];
	size_t active_blocks;
	size_t max_blocks;
} grow_bitmap_t;

static inline void grow_bitmap_init(grow_bitmap_t* gb, uintptr_t base_vaddr,
                                    size_t page_size, size_t max_capacity_mb) {
	gb->base_vaddr = base_vaddr;
	gb->page_size = page_size;
	gb->active_blocks = 1;

	gb->max_blocks = (max_capacity_mb + 127) / 128;
	if (gb->max_blocks == 0)
		gb->max_blocks = 1;
	if (gb->max_blocks > GROW_BITMAP_ABSOLUTE_MAX_BLOCKS)
		gb->max_blocks = GROW_BITMAP_ABSOLUTE_MAX_BLOCKS;

	for (int i = 0; i < GROW_BITMAP_ABSOLUTE_MAX_BLOCKS; i++)
		gb->blocks_phys[i] = 0;

	uintptr_t phys = (uintptr_t)phys_base_alloc(1);
	gb->blocks_phys[0] = phys;

	uintptr_t virt = 0;
	mem_create_physwindow(phys, &virt,
	                      PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
	                          PHYS_WINDOW_FLAG_LOCK);
	uint64_t* bitmap = (uint64_t*)virt;
	for (int i = 0; i < GROW_BITMAP_WORDS_PER_BLOCK; i++)
		bitmap[i] = 0;
	mem_release_physwindow(virt);
}

static inline uintptr_t grow_bitmap_alloc_pages(grow_bitmap_t* gb,
                                                size_t page_count) {
	if (page_count == 0 || page_count > GROW_BITMAP_BLOCK_PAGES)
		return 0;

	size_t consecutive = 0;
	size_t start_bit = 0;

	for (size_t b = 0; b < gb->active_blocks; b++) {
		uintptr_t phys = gb->blocks_phys[b];
		uintptr_t virt = 0;
		mem_create_physwindow(phys, &virt,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);
		uint64_t* bitmap = (uint64_t*)virt;

		consecutive = 0;

		for (size_t i = 0; i < GROW_BITMAP_WORDS_PER_BLOCK; i++) {
			if (bitmap[i] == 0xFFFFFFFFFFFFFFFFULL) {
				consecutive = 0;
				continue;
			}
			for (size_t bit = 0; bit < 64; bit++) {
				if ((bitmap[i] & (1ULL << bit)) == 0) {
					if (consecutive == 0) {
						start_bit = i * 64 + bit;
					}
					consecutive++;
					if (consecutive == page_count) {
						for (size_t j = 0;
						     j < page_count; j++) {
							size_t bit_idx =
							    start_bit + j;
							bitmap[bit_idx / 64] |=
							    (1ULL
							     << (bit_idx % 64));
						}
						mem_release_physwindow(virt);
						return gb->base_vaddr +
						       (b * GROW_BITMAP_BLOCK_PAGES +
						        start_bit) *
						           gb->page_size;
					}
				} else {
					consecutive = 0;
				}
			}
		}
		mem_release_physwindow(virt);
	}

	// Grow eksponensial (2x lipat) saat kehabisan ruang
	if (gb->active_blocks < gb->max_blocks) {
		size_t blocks_to_add =
		    gb->active_blocks; // Doubling the capacity
		if (gb->active_blocks + blocks_to_add > gb->max_blocks) {
			blocks_to_add = gb->max_blocks - gb->active_blocks;
		}

		size_t start_new_b = gb->active_blocks;

		for (size_t i = 0; i < blocks_to_add; i++) {
			size_t new_b = gb->active_blocks++;
			uintptr_t phys = (uintptr_t)phys_base_alloc(1);
			gb->blocks_phys[new_b] = phys;

			uintptr_t virt = 0;
			mem_create_physwindow(phys, &virt,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
			uint64_t* bitmap = (uint64_t*)virt;

			for (int k = 0; k < GROW_BITMAP_WORDS_PER_BLOCK; k++)
				bitmap[k] = 0;

			// Alokasikan dari blok baru yang pertama kali dibuat dalam batch ini
			if (i == 0) {
				for (size_t j = 0; j < page_count; j++) {
					bitmap[j / 64] |= (1ULL << (j % 64));
				}
			}
			mem_release_physwindow(virt);
		}

		return gb->base_vaddr +
		       (start_new_b * GROW_BITMAP_BLOCK_PAGES) * gb->page_size;
	}

	return 0;
}

static inline uintptr_t grow_bitmap_alloc(grow_bitmap_t* gb) {
	return grow_bitmap_alloc_pages(gb, 1);
}

static inline void grow_bitmap_free_pages(grow_bitmap_t* gb, uintptr_t vaddr,
                                          size_t page_count) {
	if (vaddr < gb->base_vaddr)
		return;

	size_t total_page_idx = (vaddr - gb->base_vaddr) / gb->page_size;
	size_t block_idx = total_page_idx / GROW_BITMAP_BLOCK_PAGES;
	size_t bit_idx = total_page_idx % GROW_BITMAP_BLOCK_PAGES;

	if (block_idx >= gb->active_blocks || !gb->blocks_phys[block_idx])
		return;

	uintptr_t phys = gb->blocks_phys[block_idx];
	uintptr_t virt = 0;
	mem_create_physwindow(phys, &virt,
	                      PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
	                          PHYS_WINDOW_FLAG_LOCK);
	uint64_t* bitmap = (uint64_t*)virt;

	for (size_t j = 0; j < page_count; j++) {
		size_t b = bit_idx + j;
		bitmap[b / 64] &= ~(1ULL << (b % 64));
	}
	mem_release_physwindow(virt);
}

static inline void grow_bitmap_free(grow_bitmap_t* gb, uintptr_t vaddr) {
	grow_bitmap_free_pages(gb, vaddr, 1);
}

#endif // __LIBK_GROW_BITMAP_H__
