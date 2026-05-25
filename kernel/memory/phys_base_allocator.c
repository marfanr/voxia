#include "phys_base_allocator.h"
#include "init/init.h"
#include "libk/stivale2.h"
#include <type.h>
#include "memory/entry.h"
#include <hal/cpu/paging.h>
#include <spinlock.h>
#include <libk/debug/debug.h>
#include <libk/serial.h>
#include <str.h>
#include <memory/memory_utils.h>

uint64_t higher_base_length_ = 0;
uint64_t usable_total = 0;
uint64_t smallest_free_entry_base = (uint64_t) -1;
size_t dma_bitmap_count = 0;

uint8_t* bitmap_base_ = 0;
uint8_t* dma_bitmap_base_ = 0;
size_t dma_bitmap_size = 0;
uint64_t metadata_count = 0;
uint64_t metadata_size = 0;
struct stivale2_struct_tag_memmap* saved_memmap_info = 0;

extern size_t
__fast_phys_base_find_free_block__(uint64_t* bitmap, size_t num_words);

static const char* pMemoryType(uint32_t type) {
	switch (type) {
	case ENTRY_MMAP_USABLE:
		return "USABLE";
	case ENTRY_MMAP_RESERVED:
		return "RESERVED";
	case ENTRY_MMAP_ACPI_RECLAIMABLE:
		return "ACPI_RECLAIMABLE";
	case ENTRY_MMAP_ACPI_NVS:
		return "ACPI_NVS";
	case ENTRY_MMAP_BAD_MEMORY:
		return "BAD_MEMORY";
	case ENTRY_MMAP_BOOTLOADER_RECLAIMABLE:
		return "BOOTLOADER_RECLAIMABLE";
	case ENTRY_MMAP_KERNEL_AND_MODULES:
		return "KERNEL_AND_MODULES";
	case ENTRY_MMAP_FRAMEBUFFER:
		return "FRAMEBUFFER";
	case 0x7373:
		return "DMA";
	default:
		return "UNKNOWN";
	}
}

INIT(phys_base_allocator) {
	LOG_INFO("PHYS", "memory entries %d", ctx->memory.memory_entries);

	memory_entry_t* smallest_entry = NULL;

	for (register uint64_t i = 0; i < ctx->memory.memory_entries; i++) {
		memory_entry_t* entry = &ctx->memory.memory_map[i];
		higher_base_length_ += entry->length;

		if (entry->type == ENTRY_MMAP_USABLE) {
			usable_total += entry->length;
		}
	}

	uintptr_t smalest_base1 = __UINT64_MAX__;
	uintptr_t smalest_base2 = __UINT64_MAX__;

	// mencari base addr terkecil ke 2 untuk keperluan DMA
	for (uint64_t i = 0; i < ctx->memory.memory_entries; i++) {
		memory_entry_t* entry = &ctx->memory.memory_map[i];

		if (entry->type != ENTRY_MMAP_USABLE)
			continue;

		if (entry->base > 0) {
			if (entry->base < smalest_base1) {
				smalest_base2 = smalest_base1;
				smalest_base1 = entry->base;
			} else if (entry->base < smalest_base2
				   && entry->base != smalest_base1) {
				smalest_base2 = entry->base;
				smallest_entry = entry;
				smallest_free_entry_base = entry->base;
			}
		}
	}

	LOG_INFO("PHYS", "smalest base 1 : 0x%x", smalest_base1);
	LOG_INFO("PHYS", "smalest base 2 : 0x%x", smalest_base2);

	if (smallest_entry == NULL) {
		LOG_ERROR("PHYS", "failed to find DMA region");
		return; // atau panic
	}

	smallest_entry->type = STIVALE2_MMAP_RESERVED;
	LOG_INFO("PHYS", "DMA base 0x%x size %d kb", smallest_free_entry_base,
		 smallest_entry->length / 1024);

	// metadata
	metadata_count = higher_base_length_ / BLOCK_SIZE / 8;
	metadata_size = ALIGN_UP(metadata_count * sizeof(uint8_t), BLOCK_SIZE);

	LOG_INFO("MEMORY", "metadata  count %d with size %d kb", metadata_count,
		 metadata_size / 1024);

	for (uint64_t i = 0; i < ctx->memory.memory_entries; i++) {
		memory_entry_t* entry = &ctx->memory.memory_map[i];

		if (entry->length >= metadata_size
		    && entry->type == STIVALE2_MMAP_USABLE) {
			LOG_INFO("MEMORY",
				 "found suitale place for metadata 0x%x length "
				 "%d kb",
				 entry->base, entry->length / 1024);

			// phys_boot_metadata = (memory_4k_block
			// *)(entry->base);
			bitmap_base_ = (uint8_t*) (entry->base);
			entry->length -= metadata_size;
			entry->base += metadata_size;
			break;
		}
	}

	memset(bitmap_base_, 0xFF, metadata_size);

	for (uint64_t i = 0; i < ctx->memory.memory_entries; i++) {
		memory_entry_t* entry = &ctx->memory.memory_map[i];

		if (entry->type != ENTRY_MMAP_USABLE)
			continue;

		uint64_t metadata_index = entry->base / 0x1000;
		uint64_t metadata_end = entry->length / 0x1000;
		for (uint64_t j = metadata_index;
		     j < (metadata_index + metadata_end); j++) {
			bitmap_base_[j / 8] &= ~(1 << (j % 8));
		}
	}

	// {
	//     smallest_entry->type    = 0x7373;
	//     uint64_t metadata_index = smallest_entry->base / 0x1000;
	//     uint64_t metadata_end   = smallest_entry->length / 0x1000;
	//     for (uint64_t j = metadata_index; j < (metadata_index +
	//     metadata_end); j++)
	//     {
	//         bitmap_base_[j / 8] |= (1 << (j % 8));
	//     }
	// }

	// find first smallest entry
	for (uint64_t i = 0; i < higher_base_length_ / BLOCK_SIZE; i++) {
		if ((bitmap_base_[i / 8] & (1 << (i % 8))) == 0) {
			LOG_INFO("MEMORY", "found smallest entry at index %d",
				 i);
			break;
		}
	}

	// mark smallest entry sebagai reserved atau digunakan
	LOG_INFO("MEMORY", "usable memory size : %d mb",
		 usable_total / 1024 / 1024);
	for (uint64_t i = 0; i < ctx->memory.memory_entries; i++) {
		memory_entry_t* entry = &ctx->memory.memory_map[i];
		LOG_INFO("MEMORY",
			 "entry %d base 0x%x -- 0x%x length %d (%d Kb) type %s",
			 i, entry->base, entry->base + entry->length,
			 entry->length, entry->length / 1024,
			 pMemoryType(entry->type));
	}

	// dma allocator
	// dma_bitmap_count = smallest_entry->length / BLOCK_SIZE / 8;
	// dma_bitmap_size  = ALIGN_UP(dma_bitmap_count * sizeof(uint8_t),
	// BLOCK_SIZE);

	// dma_bitmap_base_ = (uint8_t *)(smallest_entry->base);
	// memset(dma_bitmap_base_, 0, dma_bitmap_size);
}

static spinlock_t pmm_lock = {0};

void* phys_base_alloc(uint64_t block) {
	spin_acquire(&pmm_lock);
	uint64_t total_blocks = higher_base_length_ / BLOCK_SIZE;
	uint64_t consecutive = 0;
	uint64_t start = 0;

	uint64_t i = 0;

	while (i < total_blocks) {
		uint64_t word_idx = i / 64;
		uint64_t bit_idx = i % 64;

		uint64_t word;
		memcopy(&word, &bitmap_base_[word_idx * 8], sizeof(word));

		word >>= bit_idx;

		if (bit_idx == 0 && word == (uint64_t) -1) {
			i += 64;
			consecutive = 0;
			continue;
		}

		if (word == (uint64_t) -1) {
			i += 64 - bit_idx;
			consecutive = 0;
			continue;
		}

		for (uint64_t b = bit_idx; b < 64 && i < total_blocks;
		     b++, i++) {
			bool used = (bitmap_base_[i / 8] >> (i % 8)) & 1;
			if (!used) {
				if (consecutive == 0)
					start = i;
				consecutive++;
				if (consecutive == block) {
					// Set bit berturut-turut
					for (uint64_t j = start;
					     j < start + block; j++)
						bitmap_base_[j / 8] |=
							(1 << (j % 8));

					spin_release(&pmm_lock);
					return (void*) (start * BLOCK_SIZE);
				}
			} else {
				consecutive = 0;
			}
		}
	}

	spin_release(&pmm_lock);
	return NULL;
}

void* phys_base_alloc_on_top(uint64_t block) {
	if (block == 0)
		return NULL;

	return NULL;
}

uint64_t pys_base_get_free_block_count() {
	uint64_t count = 0;
	uint64_t total_blocks = higher_base_length_ / BLOCK_SIZE;

	for (uint64_t i = 0; i < total_blocks; i++) {
		if (!(bitmap_base_[i / 8] & (1 << (i % 8)))) {
			count++;
		}
	}

	return count;
}

void vxPhysBaseFree(void* ptr, uint64_t size) {
	spin_acquire(&pmm_lock);
	uint64_t index = (uint64_t) ptr / BLOCK_SIZE;
	for (uint64_t i = index; i < index + size; i++) {
		bitmap_base_[i / 8] &= ~(1 << (i % 8));
	}
	spin_release(&pmm_lock);
}

void pmm_log_usage() {
	uint64_t total_blocks = usable_total / BLOCK_SIZE;
	uint64_t free_blocks = pys_base_get_free_block_count();
	uint64_t used_blocks = total_blocks - free_blocks;

	KDEBUG(DEBUG_LEVEL_INFO, "used memory : %d mb / %d mb (%d mb free)\n",
	       used_blocks * BLOCK_SIZE / 1024 / 1024,
	       total_blocks * BLOCK_SIZE / 1024 / 1024,
	       free_blocks * BLOCK_SIZE / 1024 / 1024);
}