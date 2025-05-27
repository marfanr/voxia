#ifndef __MEMORY_BUDDY_ALLOCATOR_H__
#define __MEMORY_BUDDY_ALLOCATOR_H__

#include <libk/type.h>

struct buddy_block
{
    size_t order;
    struct buddy_block *next;
};

#define BUDDY_DEFAULT_MIN_ORDER 4  // 2^4 = 16 byte
#define BUDDY_DEFAULT_MAX_ORDER 64 // 2 ^ 64 = 1.844674407×10¹⁹ byte

struct buddy_list
{
    struct buddy_block *free_block;
    size_t free_count;
};

struct buddy_allocator
{
    struct buddy_list *lists[BUDDY_DEFAULT_MAX_ORDER];
    size_t order;
};

struct buddy_allocator *buddy_allocator_install (void *base, size_t size);
void *buddy_alloc (struct buddy_allocator *block, size_t size);
size_t buddy_find_visible_size (struct buddy_allocator *block, size_t size);
void buddy_log (struct buddy_allocator *block);
int buddy_free (struct buddy_allocator *base, void *ptr);
#endif // __MEMORY_BUDDY_ALLOCATOR_H__