#ifndef __HAL__CPU__PAGING_H__
#define __HAL__CPU__PAGING_H__

#include <type.h>

// TODO: since we supported multiple page size not only 4kb now is invalid
#define PAGE_SIZE_4KB 0x1000ULL
#define PAGE_SIZE_2MB 0x200000ULL
#define PAGE_SIZE_1GB 0x40000000ULL

#define GB 0x40000000UL
#define MB 0x100000ULL

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

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER (1ULL << 2)
#define PAGE_WRITE_THROUGH (1ULL << 3)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_ACCESSED (1ULL << 5)
#define PAGE_DIRTY (1ULL << 6)
#define PAGE_HUGE (1ULL << 7)
#define PAGE_GLOBAL (1ULL << 8)
#define PAGE_COW (1ULL << 9)
#define PAGE_NO_EXECUTE (1ULL << 63)

// Hardware flags mask for Level 0 PTE (excludes Huge and Software bits)
#define PAGE_HW_FLAG (0xF7FULL | PAGE_NO_EXECUTE)

// Internal software flags for mmap (stripped before hardware write).
// Placed at bits 61-62 to reserve standard PTE software bits (9-11) for future use.
#define PAGE_2MB (1ULL << 61)
#define PAGE_1GB (1ULL << 62)

#define PAGE_TABLE_INDEX_MASK 0x1FFULL
#define PAGE_PHYS_MASK 0x000FFFFFFFFFF000ULL
#define PAGE_INTER_STRIP                                                       \
	(0xFFFULL << 52 | (1ULL << 7) | (1ULL << 4) | (1ULL << 3))

void paging_mmap(page_t page_dir, uint64_t virt, uint64_t phys, uint64_t flags);
void paging_reload(page_t pml4);
page_t paging_get_highest_page_map(void);
void paging_unmap_page(page_t page_dir, uint64_t virt);
void paging_multiple_unmap(page_t page_dir, uint64_t virt, size_t size);
void paging_setup(page_t pml4);
void paging_sync_kernel_entry(uint64_t vaddr);

void paging_multiple_mmap(page_t page_dir, uint64_t virt, uint64_t phys,
                          uint64_t size, uint64_t flags);
uint64_t vaddr_to_paddr(page_t pml4, uint64_t vaddr);
void paging_add_dma_mapping(uintptr_t phys, uintptr_t virt, uint64_t size);
void paging_debug(page_t pml4, uint64_t virt);

page_t paging_create_page_directory();

void paging_make_cow(page_t page_dir, uint64_t virt);
uint64_t paging_get_entry(page_t page_dir, uint64_t virt);
uint64_t paging_get_entry_ext(page_t page_dir, uint64_t virt, int* level_out);

#define INVLPG(x) asm volatile("invlpg (%0)" ::"r"(x) : "memory");

#endif // __HAL__CPU__PAGING_H__
