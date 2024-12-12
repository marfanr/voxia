#ifndef __MEMORY_BUDDY_ALLOCATOR_H__
#define __MEMORY_BUDDY_ALLOCATOR_H__

#include <libk/type.h>

typedef struct buddy_block {
  size_t size;
  void *ptr;
  bool used;
} buddy_block_t;

#define BUDDY_DEFAULT_MIN_ORDER 8  // 2^8 = 256 byte
#define BUDDY_DEFAULT_MAX_ORDER 15 // 2 ^ 15 = 32768 byte

buddy_block_t *buddy_allocator_install(void *base, size_t size);
void *buddy_alloc(buddy_block_t *block, size_t size);
void buddy_log(buddy_block_t *block);

#endif // __MEMORY_BUDDY_ALLOCATOR_H__