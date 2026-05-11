#ifndef __HAL__CPU__PAGING_H__
#define __HAL__CPU__PAGING_H__

#include <type.h>

#define PAGE_SIZE 0x1000

#define GB 0x40000000UL // 1GB
#define MB 0x1000

typedef volatile uint64_t* page_t;
typedef struct {
	uintptr_t phys;
	uintptr_t virt;
	uint64_t size;
	volatile void* next;
} dma_memory_mapping_t;

typedef struct reserve_map reserve_map;
struct reserve_map {
	uintptr_t p_addr;
	uintptr_t v_addr;
	size_t count;
	int flags;

	reserve_map* next;
} __attribute__((aligned(32)));

page_t paging_create_page_directory();

#define VMM_PAGE paging_create_page_directory()

enum {
	PAGE_PRESENT = 1U << 0,
	PAGE_WRITABLE = 1U << 1,
	PAGE_USER = 1U << 2,
	PAGE_WRITE_THROUGH = 1U << 3,
	PAGE_CACHE_DISABLE = 1U << 4,
	PAGE_ACCESSED = 1U << 5,
	PAGE_DIRTY = 1U << 6,
	PAGE_HUGE = 1U << 7,
	PAGE_GLOBAL = 1U << 8,
	PAGE_NO_EXECUTE = 1U << 63,
} __attribute__((enum_extensibility(closed)));

void vxMmap(page_t page_dir, uint64_t virt, uint64_t phys, int flags);
void paging_reload(page_t pml4);
page_t paging_get_highest_page_map(void);
void paging_unmap_page(page_t page_dir, uint64_t virt);
void paging_unmap_fill(page_t page_dir, uint64_t virt, size_t size);
void paging_setup(page_t pml4);
void paging_fork(page_t parent_pml4, page_t child_pml4);
void vxMultipleMmap(page_t page_dir, uint64_t virt, uint64_t phys,
		    uint64_t size, int flags);
uint64_t vaddr_to_paddr(page_t pml4, uint64_t vaddr);
void paging_add_dma_mapping(uintptr_t phys, uintptr_t virt, uint64_t size);
void paging_debug(page_t pml4, uint64_t virt);
#endif // __HAL__CPU__PAGING_H__
