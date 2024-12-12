#ifndef __HAL__CPU__PAGING_H__
#define __HAL__CPU__PAGING_H__

#include <libk/type.h>

#define VMM_PAGE_SIZE 0x1000

#define GB 0x40000000UL
#define MB 0x1000

typedef uint64_t *page_t;

page_t paging_create_page_directory();

#define VMM_PAGE paging_create_page_directory()
void paging_install();
void paging_mmap(page_t page_dir, uint64_t virt, uint64_t phys, int flags);
void paging_reload(page_t pml4);
page_t paging_get_highest_page_map(void);
void paging_unmap_page(page_t page_dir, uint64_t virt);
void paging_setup(page_t pml4);
void paging_fork(page_t parent_pml4, page_t child_pml4);

#endif // __HAL__CPU__PAGING_H__
