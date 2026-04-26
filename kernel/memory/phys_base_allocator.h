#ifndef __MEMORY__ALLOCATOR_H__
#define __MEMORY__ALLOCATOR_H__

#include <libk/stivale2.h>
#include <libk/type.h>

#define BLOCK_SIZE (uint64_t)0x1000

enum MEMORY_ENTRY_TYPE {
	RESERVED,
	USABLE,
} __attribute__((__enum_extensibility__(closed)));

typedef struct {

} memory_region;

void phys_base_allocator_install(
    struct stivale2_struct_tag_memmap* stivale_memmap);
void* vxPhysBaseAlloc(uint64_t size);
void* phys_base_alloc_aligned(uint64_t block, uint64_t align);
void vxPhysBaseFree(void* page, uint64_t length);
void pmm_log_usage();
uint64_t pys_base_get_free_block_count();
void* phys_base_alloc_on_top(uint64_t block);

void* pDMAalloc(uint64_t block);
void dma_free(void* ptr, uint64_t size);

extern uint64_t bitmap_size_;

#endif // __MEMORY__ALLOCATOR_H__
