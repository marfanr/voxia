#ifndef __MEMORY__ALLOCATOR_H__
#define __MEMORY__ALLOCATOR_H__

#include <hal/cpu/paging.h>
#include <libk/stivale2.h>
#include <libk/type.h>

#define BLOCK_SIZE 0x1000

enum MEMORY_ENTRY_TYPE
{
    RESERVED,
    USABLE,
} __attribute__((__enum_extensibility__(closed)));

// @deprecated.
// consume much ram
typedef struct memory_4k_block memory_4k_block;
struct memory_4k_block
{
    uintptr_t address;
    boolean_t used;
    // memory_4k_block *next;
    // memory_4k_block *prev;
} __attribute__((aligned(64)));

typedef struct
{
    uint64_t         base;
    uint64_t         length;
    int              type;
    memory_4k_block *block;
} memory_entry;

typedef struct
{

} memory_region;

void     phys_base_allocator_install(struct stivale2_struct_tag_memmap *stivale_memmap);
void    *phys_base_alloc(uint64_t size);
void    *phys_base_alloc_aligned(uint64_t block, uint64_t align);
void     phys_base_free(void *page, uint64_t length);
void     pmm_log_usage();
uint64_t pys_base_get_free_block_count();
void    *phys_base_alloc_on_top(uint64_t block);

extern uint64_t bitmap_size_;

#endif // __MEMORY__ALLOCATOR_H__
