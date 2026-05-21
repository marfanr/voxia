#ifndef __HAL__CPU__PAGING_H__
#define __HAL__CPU__PAGING_H__

#include <type.h>

#define PAGE_SIZE 0x1000

#define GB 0x40000000UL
#define MB 0x100000ULL // fix: MB harusnya 1MB = 0x100000, bukan 0x1000

typedef volatile uintptr_t* page_t;

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

// Semua flag pakai ULL — cegah overflow dan sign-extension
#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER (1ULL << 2)
#define PAGE_WRITE_THROUGH (1ULL << 3)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_ACCESSED (1ULL << 5)
#define PAGE_DIRTY (1ULL << 6)
#define PAGE_HUGE (1ULL << 7)
#define PAGE_GLOBAL (1ULL << 8)
#define PAGE_NO_EXECUTE (1ULL << 63)

// Mask untuk strip flag dari physical address
#define PAGE_PHYS_MASK 0x000FFFFFFFFFF000ULL
//                          ↑ mask out bit 63 (NX) dan bit 0-11 (flags)
// Mask untuk strip reserved bits dan cache bits dari intermediate entries
#define PAGE_INTER_STRIP                                                       \
	(0xFFFULL << 52 | (1ULL << 7) | (1ULL << 4) | (1ULL << 3))

void vxMmap(page_t page_dir, uint64_t virt, uint64_t phys, uint64_t flags);
void paging_reload(page_t pml4);
page_t paging_get_highest_page_map(void);
void paging_unmap_page(page_t page_dir, uint64_t virt);
void paging_unmap_fill(page_t page_dir, uint64_t virt, size_t size);
void paging_setup(page_t pml4);
void paging_fork(page_t parent_pml4, page_t child_pml4);
void vxMultipleMmap(page_t page_dir, uint64_t virt, uint64_t phys,
                    uint64_t size, uint64_t flags);
uint64_t vaddr_to_paddr(page_t pml4, uint64_t vaddr);
void paging_add_dma_mapping(uintptr_t phys, uintptr_t virt, uint64_t size);
void paging_debug(page_t pml4, uint64_t virt);

page_t paging_create_page_directory();

#endif // __HAL__CPU__PAGING_H__