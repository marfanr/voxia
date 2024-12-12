#include "buddy_allocator.h"
#include "memory_utils.h"
#include "phys_base_allocator.h"

#include <libk/serial.h>
#include <libk/str/memset.h>

int log2(size_t n) {
  int i = 0;
  while (n >>= 1) {
    i++;
  }
  return i;
}

int find_order(size_t size) {
  size_t min_order = BUDDY_DEFAULT_MIN_ORDER;
  size_t max_order = BUDDY_DEFAULT_MAX_ORDER;
  // override default order
#ifdef BUDDY_MIN_ORDER
  min_order = BUDDY_MIN_ORDER;
#endif
#ifdef BUDDY_MAX_ORDER
  max_order = BUDDY_DEFAULT_MAX_ORDER;
#endif
  int order = 0;
  size = size > (1 << min_order) ? size : (1 << min_order);
  while ((1 << order) < size && order <= max_order) {
    order++;
  }
  return order;
}

size_t round2(size_t size) { return 1 << log2(size); }

buddy_block_t *buddy_allocator_install(void *base, size_t size) {
  serial_trace("pure size : %d bits\n", size);
  size_t order = find_order(size);
  size_t min_order = BUDDY_DEFAULT_MIN_ORDER;
#ifdef BUDDY_MIN_ORDER
  min_order = BUDDY_MIN_ORDER;
#endif
  size_t metadata_size = (2 ^ (order - min_order)) * sizeof(buddy_block_t);
  serial_trace("metadata_size : %d byte \n", metadata_size);
  memset(base, 0, metadata_size);

  order = find_order(size - metadata_size);
  serial_trace("order : %d\n", order);
  buddy_block_t *block = (buddy_block_t *)(base);

  serial_trace("buddy base : 0x%x\n", ((uintptr_t)base + metadata_size));

  block->ptr = (void *)((uintptr_t)base + metadata_size);
  serial_trace("buddy 1st ptr : 0x%x\n", (uintptr_t)block->ptr);

  block->size = (size - metadata_size);
  serial_trace("buddy size : %d bits\n", block->size);
  block->used = 0;
  return block;
}

void move_block(buddy_block_t *old, buddy_block_t *new) {
  new->size = old->size;
  new->ptr = old->ptr;
  new->used = old->used;
  memset(old, 0, sizeof(buddy_block_t));
}

buddy_block_t *split(buddy_block_t *block) {
  size_t orde = find_order(block->size);
  size_t new_size = (block->size / 2);
  block->size = new_size;
  buddy_block_t *next =
      (buddy_block_t *)((uintptr_t)block + sizeof(buddy_block_t));

  // assume that the next block is already used
  size_t skip = 0;
  while (next->size != 0) {
    next = (buddy_block_t *)((uintptr_t)next + sizeof(buddy_block_t));
    skip++;
  }
  for (size_t i = 0; i < skip; i++) {
    buddy_block_t *prev =
        (buddy_block_t *)((uintptr_t)next - sizeof(buddy_block_t));
    move_block(prev, next);
    next = (buddy_block_t *)((uintptr_t)next - sizeof(buddy_block_t));
  }

  buddy_block_t *target =
      (buddy_block_t *)((uintptr_t)block + sizeof(buddy_block_t));
  target->size = new_size;
  target->ptr = (void *)((uint64_t)block->ptr + new_size);
  return block;
}

void *buddy_alloc(buddy_block_t *block, size_t size) {
  size_t n = find_order(size);

  size_t curr_order = find_order(block->size);
  uint64_t first_block = (uint64_t)block;

  // split
  buddy_block_t *next = (buddy_block_t *)((uintptr_t)block);

  while (n != curr_order || next->used == 1) {
    if (n < curr_order) {
      next = split(next);
      curr_order--;
    } else if (n > curr_order) {
      if (next->ptr == 0)
        break;
      next = (buddy_block_t *)((uintptr_t)next + sizeof(buddy_block_t));
      curr_order = find_order(next->size);
    } else if (n == curr_order && next->used == 1) {
      if (next->ptr == 0)
        break;
      next = (buddy_block_t *)((uintptr_t)next + sizeof(buddy_block_t));
      curr_order = find_order(next->size);
    }
  }

  next->used = 1;
  block = (uint64_t)first_block;
  return next->ptr;
}

void buddy_log(buddy_block_t *block) {
  buddy_block_t *next = (buddy_block_t *)((uintptr_t)block);

  serial_trace("<<begin\n");
  while (next->ptr != 0) {
    serial_trace("orde %d >> block 0x%x size %d used %d\n",
                 find_order(next->size), (uint64_t)next->ptr, next->size,
                 next->used);
    next = (buddy_block_t *)((uintptr_t)next + sizeof(buddy_block_t));
  }
  serial_trace("end>>\n");
}
