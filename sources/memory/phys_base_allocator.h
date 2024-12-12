#ifndef __MEMORY__ALLOCATOR_H__
#define __MEMORY__ALLOCATOR_H__

#include <libk/stivale2.h>
#include <libk/type.h>

void phys_base_allocator_install(
    struct stivale2_struct_tag_memmap *stivale_memmap);
void *phys_base_alloc(uint64_t size);
void phys_base_free(void *page, uint64_t length);

extern uint64_t bitmap_size_;

#endif // __MEMORY__ALLOCATOR_H__