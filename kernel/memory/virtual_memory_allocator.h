#ifndef __MEMORY__VIRTUAL_MEMORY_ALLOCATOR_H__
#define __MEMORY__VIRTUAL_MEMORY_ALLOCATOR_H__

#include "hal/cpu/spinlock.h"
#include <libk/type.h>

#define VMA_ZONE_SIZE_COUNT 6

typedef enum
{
    VM_ZONE_8    = 8,
    VM_ZONE_16   = 16,
    VM_ZONE_64   = 64,
    VM_ZONE_128  = 128,
    VM_ZONE_256  = 256,
    VM_ZONE_512  = 512,
    VM_ZONE_1024 = 1024,
    VM_ZONE_4096 = 4096,
} virtual_memory_zone_size;

enum mem_vma_base
{
    vma_zone_start                = 0xFFFFE00000000000,
    vma_zone_queue_metadata_start = 0xFFFFD00000000000,
    vma_tree_start                = 0xFFFFD000F0000000,
    mem_vma_phys_window_start     = 0xFFFFB00000000000,
    mem_vma_phys_window_pt        = 0xFFFFB0000F000000,
} __attribute__((enum_extensibility(closed)));

typedef struct virtual_memory_queue_list virtual_memory_queue_list;
struct virtual_memory_queue_list
{
    uintptr_t                  start;
    virtual_memory_queue_list *next;
} __attribute__((aligned(16)));

typedef const char virtual_memory_zone_name[20];

typedef struct virtual_memory_zone virtual_memory_zone;
struct virtual_memory_zone
{
    boolean_t                  active;
    spinlock_t                 lock;
    virtual_memory_zone_name   name;
    virtual_memory_zone_size   size;
    virtual_memory_queue_list *queue;
} __attribute__((aligned(64)));

typedef struct virtual_memory virtual_memory;
struct virtual_memory
{
    spinlock_t      used;
    uintptr_t       virt_address;
    uintptr_t       phys_address;
    size_t          length;
    int             flags;
    virtual_memory *parent;
    virtual_memory *left_child;
    virtual_memory *right_child;
} __attribute__((aligned(16)));

uint64_t vma_request_zone(virtual_memory_zone_name name);

#endif // __MEMORY__VIRTUAL_MEMORY_ALLOCATOR_H__
