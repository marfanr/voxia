#ifndef __MEMORY_KALLOC_H__
#define __MEMORY_KALLOC_H__

#include "./buddy_allocator.h"
#include <libk/type.h>

enum KALLOC_REGION
{
    KALLOC_REGION_INIT,
    KALLOC_REGION_BLOCK,
    KALLOC_REGION_VFS,
    KALLOC_REGION_QH,
    KALLOC_REGION_DEFAULT,
};

struct kalloc_region
{
    uint16_t id;
    uint64_t start;
    size_t size;
    size_t ussage;
    struct buddy_allocator *block;
};

#define KALLOC_MAX_REGION 32

void kalloc_init ();
void kalloc_region_add (uint16_t id, uint64_t start, size_t size);
void kalloc_switch_region (uint16_t id);
void *kalloc (size_t size);
void kalloc_align (size_t size, size_t align);
void kfree (void *ptr);
void kalloc_log ();
#endif // __MEMORY_KALLOC_H__
