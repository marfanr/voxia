#include "buddy.h"
#include "memory_utils.h"

#include <libk/serial.h>
#include <libk/str/memset.h>

#define BUDDY2_DEBUG 1

uint64_t
buddy_round_up(uint64_t size, uint64_t align) {
    if (size % align == 0)
        return size;
    else
        return size + (align - (size % align));
}

int get_order(size_t size) {
    int order = 0;
    while (size > 1) {
        size >>= 1;
        order++;
    }
    return order;
}

/*
 * buddy_init:
 *  - base: pointer ke memory block, kalau NULL, gunakan allocator
 *  - total_size: total ukuran buddy allocator (harus power of two)
 *  - allocator: fungsi alokasi, contoh: malloc
 */
void buddy2_init(BuddyAllocator *allocator, void *base, size_t total_size) {
    if (total_size == 0) {
        serial_trace("Ukuran total tidak boleh \n");
        return;
    }

    int order = get_order(total_size);

    // Dapatkan memory pool

    // Alokasi free_list array secara dinamis

    for (int i = 0; i <= MAX_ORDER; i++) {
        allocator->free_list[i] = NULL;
    }
    // Inisialisasi free_list di level tertinggi dengan satu blok besar
    allocator->free_list[order] = (Block *)base;
    serial_trace("Buddy2: alokasi free list\n");
    allocator->free_list[order]->next = NULL;
    allocator->memory_pool            = base;
    allocator->total_size             = total_size;
    serial_trace("Buddy2: inisialisasi allocator %x\n", base);
}

// Alokasi buddy
void *
buddy2_alloc(BuddyAllocator *allocator, size_t size) {
    int order     = get_order(size);
    int max_order = get_order(allocator->total_size);
    if (order > max_order) {
        serial_trace("     | Buddy: Ukuran melebihi batas maksimum\n");
        return NULL;
    }

    // Cari blok yang bebas di level >= order
    for (int i = order; i <= max_order; i++) {
        if (allocator->free_list[i]) {
            Block *block            = allocator->free_list[i];
            allocator->free_list[i] = block->next;
            // Split blok sampai mencapai level yang diinginkan
            while (i > order) {
                i--;
                Block *buddy = (Block *)((uintptr_t)block + (1UL << i));

                buddy->next             = allocator->free_list[i];
                allocator->free_list[i] = buddy;
            }
            return block;
        }
    }
    return NULL;
}

// Free buddy, menggabungkan blok bila memungkinkan
void buddy2_free(BuddyAllocator *allocator, void *ptr, size_t size) {
    int order     = get_order(size);
    int max_order = get_order(allocator->total_size);
    // Hitung offset relatif terhadap memory_pool
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)allocator->memory_pool;

    // Coba merge terus selama buddy ada di free_list
    while (order < max_order) {
        uintptr_t buddy_offset = offset ^ (1UL << order);
        Block    *buddy        = (Block *)((uintptr_t)allocator->memory_pool + buddy_offset);

        // Cari buddy di free_list[order]
        Block **prev  = &allocator->free_list[order];
        boolean_t    found = 0;
        while (*prev) {
            if (*prev == buddy) {
                found = 1;
                break;
            }
            prev = &((*prev)->next);
        }
        if (!found)
            break;

        // Hapus buddy dari free_list dan update offset ke alamat terkecil
        *prev = buddy->next;
        if (buddy_offset < offset)
            offset = buddy_offset;
        order++;
    }
    Block *new_block            = (Block *)((uintptr_t)allocator->memory_pool + offset);
    new_block->next             = allocator->free_list[order];
    allocator->free_list[order] = new_block;
}

void buddy2_print(BuddyAllocator *allocator) {
    for (int i = 0; i <= MAX_ORDER; i++) {
        Block *block = allocator->free_list[i];
        serial_trace("Free list %d: ", i);
        while (block) {
            serial_trace("%x ", block);
            block = block->next;
        }
        serial_trace("\n");
    }
}
