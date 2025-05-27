#include "buddy_allocator.h"
#include "memory_utils.h"

#include <libk/serial.h>
#include <libk/str/memset.h>

#define BUDDY_DEBUG 0

int
find_order (size_t size)
{
    size_t min_order = BUDDY_DEFAULT_MIN_ORDER;
    size_t max_order = BUDDY_DEFAULT_MAX_ORDER;
    int order = 1;
    size = size > (1 << min_order) ? size : (1 << min_order);
    while ((1 << order) < size && order <= max_order)
        {
            order++;
        }
    return order;
}

extern uint64_t pow (uint64_t base, uint64_t exp);

static uint64_t
buddy_calc_metadata_size (size_t order)
{
    return sizeof (struct buddy_allocator)
           + (order * sizeof (struct buddy_list))
           + sizeof (struct buddy_block);
}

struct buddy_allocator *
buddy_allocator_install (void *base, size_t size)
{
    memset (base, 0, size);
    size_t order = find_order (size);
#if BUDDY_DEBUG
    serial_trace (" | buddy_alloc: \npure size : %d bits, order : %d\n", size,
                  order);
#endif
    size_t metadata_size = buddy_calc_metadata_size (order);
    order = find_order (size - metadata_size);
#if BUDDY_DEBUG
    serial_trace (" | buddy_alloc: metadata_size :  %d bits \n",
                  metadata_size);
    serial_trace (" | buddy_alloc: block size :  %d bits \n",
                  sizeof (struct buddy_block));
#endif
    struct buddy_allocator *buddy_base = (struct buddy_allocator *)base;

    // initialize buddy list
    for (int i = 0; i < order; i++)
        {
            struct buddy_list *list
                = (struct buddy_list *)((uint64_t)base
                                        + sizeof (struct buddy_allocator)
                                        + (i * sizeof (struct buddy_list)));
            list->free_block = 0;
            list->free_count = 0;
            buddy_base->lists[i] = list;
        }

    // fill first list
    struct buddy_block *first_block
        = (struct buddy_block *)((uintptr_t)base + metadata_size + 64);
    first_block->next = 0;
    first_block->order = order;

    buddy_base->lists[order - 1]->free_block = first_block;
    buddy_base->lists[order - 1]->free_count = 1;
    buddy_base->order = order;

    return buddy_base;
}

#define container_of(ptr, type, member)                                       \
    ((type *)((uintptr_t)ptr - (uintptr_t)(&((type *)0)->member)))

#define list_fist_entry(ptr, type, member)                                    \
    container_of ((ptr)->next, type, member)

static struct buddy_block *
split (struct buddy_allocator *base, size_t idx)
{
    struct buddy_block *block = base->lists[idx - 1]->free_block;

    base->lists[idx - 1]->free_block = block->next;
    base->lists[idx - 1]->free_count--;

    size_t block_current_size = pow (2, idx);

    // split block into 2
    struct buddy_block *new_block
        = (struct buddy_block *)((uintptr_t)block + (block_current_size / 2));
    memset (new_block, 0, block_current_size / 2);
    new_block->next = 0;
    new_block->order = idx - 1;

    block->next = new_block;
    block->order = idx - 1;

    // add new block to prev order
    base->lists[idx - 2]->free_block = block;
    base->lists[idx - 2]->free_count += 2;
#if BUDDY_DEBUG
    serial_trace (
        " | buddy_alloc: free block %d prev count : %d into count : %d\n",
        idx - 1, base->lists[idx - 1]->free_count,
        base->lists[idx - 2]->free_count);

    serial_trace (" | buddy_alloc: splitted block order : %d, block : "
                  "\033[34m\033[34m0x%x\033[0m\033[0m\n",
                  idx, block);
#endif
    return block;
}

size_t
buddy_find_visible_size (struct buddy_allocator *block, size_t size)
{
    size_t order = find_order (size + sizeof (struct buddy_block));

    return pow (2, order);
}

void *
buddy_alloc (struct buddy_allocator *base, size_t size)
{
    struct buddy_list *list;
    struct buddy_block *block;
    size_t order = find_order (size + sizeof (struct buddy_block));
    size_t current_order = order;

#if BUDDY_DEBUG
    serial_trace (" | buddy_alloc: order : %d  base order : %d\n", order,
                  base->order);
#endif
    for (; current_order <= base->order; current_order++)
        {
            list = base->lists[current_order - 1];
#if BUDDY_DEBUG
            serial_trace (
                " | buddy_alloc: want %d current order : %d, order : %d  "
                "free block : %d\n",
                order, current_order, base->order, list->free_count);
#endif
            if (list->free_count > 0)
                {
                    block = list->free_block;
                    serial_trace (" | buddy_alloc: found block : "
                                  "\033[34m0x%x\033[0m\n",
                                  block);

                    while (current_order != order)
                        {
                            if (current_order > order)
                                {
                                    block = split (base, current_order);
                                    current_order--;
                                }
                            else
                                {
                                    serial_trace (" | buddy_alloc: ERROR: not "
                                                  "handled\n");
                                    for (;;)
                                        ;
                                }
                        }

                    list = base->lists[current_order - 1];
                    list->free_block = block->next;
                    list->free_count--;
                    block->order = current_order;
#if BUDDY_DEBUG
                    serial_trace (" | buddy_alloc: allocated block : "
                                  "\033[34m0x%x\033[0m "
                                  "current order %d (real %d)  next 0x%x\n",
                                  block, current_order, block->order,
                                  block->next);
#endif

                    void *res = ((void *)((uintptr_t)block
                                          + sizeof (struct buddy_block)));
                    memset (res, 0,
                            pow (2, current_order)
                                - sizeof (struct buddy_block));
                    return res;
                }
        }

    serial_trace (" | buddy_alloc: ERROR: out of memory\n");
    buddy_log (base);
    return NULL;
}

int
buddy_free (struct buddy_allocator *base, void *ptr)
{
    if (ptr == NULL)
        {
            serial_trace (" | buddy alloc: ERROR: free NULL pointer\n");
            return -1;
        }
    struct buddy_block *block
        = (struct buddy_block *)((uintptr_t)ptr - sizeof (struct buddy_block));

    size_t current_order = block->order;

#if BUDDY_DEBUG
    serial_trace (" | buddy_alloc (free): block 0x%x current order : %d\n",
                  block, current_order);
#endif

    block->next = base->lists[current_order - 1]->free_block;
    base->lists[current_order - 1]->free_block = block;
    base->lists[current_order - 1]->free_count++;
    block->order = current_order;

#if BUDDY_DEBUG
    serial_trace (" | buddy_alloc (free ok) prev count: %d: free block "
                  ":\033[34m0x%x\033[0m next 0x%x count %d\n",
                  base->lists[current_order - 1]->free_count,
                  base->lists[current_order - 1]->free_block, block->next,
                  base->lists[current_order]->free_count);
#endif

    // merging
    if (base->lists[current_order - 1]->free_count > 1
        && block->next->next != 0 && current_order < base->order)
        {
            serial_trace (" | buddy_alloc (free): merged block : "
                          "\033[34m0x%x\033[0m into order : %d "
                          "count befored merge %d\n",
                          block, current_order + 1,
                          base->lists[current_order - 1]->free_count);
            // base->lists[current_order - 1]->free_block = block->next->next;
            // base->lists[current_order - 1]->free_count -= 2;

            // // memset (block, 0, pow (2, block->));
            // block->next = base->lists[current_order]->free_block;
            // base->lists[current_order]->free_count++;
            // base->lists[current_order]->free_block = block;
            // block->order = current_order + 1;
        }
    return 1;
}

void
buddy_log (struct buddy_allocator *base)
{
    size_t free = 0;
    size_t metadata_size = sizeof (struct buddy_allocator)
                           + (base->order * sizeof (struct buddy_list));
    for (int i = 0; i < base->order; i++)
        {
            if (base->lists[i]->free_count == 0)
                {
                    continue;
                }
            size_t block_size = pow (2, i + 1);
            free += block_size * base->lists[i]->free_count;
        }
    size_t used = pow (2, base->order) - free + metadata_size;
    serial_trace (" | buddy_alloc: used : %d Mb \n", used / 1024 / 1024);
}
