#ifndef __MEMORY_BUDDY2_ALLOCATOR_H__
#define __MEMORY_BUDDY2_ALLOCATOR_H__

#include <libk/type.h>

#define MAX_ORDER 32 // Maksimum level buddy (2^10 = 1024 blok)

typedef struct Block {
  struct Block *next;
} Block;

typedef struct {
  Block *free_list[MAX_ORDER + 1];
  void *memory_pool;
  size_t total_size;
} __attribute__((deprecated("we will not using buddy anymore"))) BuddyAllocator;

void buddy2_init(BuddyAllocator *allocator, void *base, size_t total_size);
void *buddy2_alloc(BuddyAllocator *allocator, size_t size);
void buddy2_free(BuddyAllocator *allocator, void *ptr, size_t size);
void buddy2_print(BuddyAllocator *allocator);
uint64_t buddy_round_up(uint64_t size, uint64_t align);

#endif // __MEMORY_BUDDY2_ALLOCATOR_H__
