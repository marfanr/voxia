#ifndef __MEMORY_VM_MANAGER_H__
#define __MEMORY_VM_MANAGER_H__

#include <hal/cpu/spinlock.h>
#include <libk/type.h>

#define VMA_ZONE_SIZE_COUNT 6

enum mem_vma_base
{
    vma_zone_start                = 0xFFFFE00000000000,
    vma_zone_queue_metadata_start = 0xFFFFD00000000000,
    vma_tree_start                = 0xFFFFC00000000000,
    vma_tree_node_start           = 0xFFFFCE0000000000,
    mem_vma_phys_window_start     = 0xFFFFB00000000000,
    mem_vma_phys_window_pt        = 0xFFFFB0000F000000,
} __attribute__((enum_extensibility(closed)));

typedef struct virtual_memory virtual_memory;
struct virtual_memory
{
    boolean_t used;
    uintptr_t start_address;
    uintptr_t end_address;
    uintptr_t phys_address;
    size_t    length;
    int       flags;
    int       core;
} __attribute__((aligned(16)));

void            vma_register(uintptr_t phys_address, uintptr_t virt_addr, size_t size);
virtual_memory *vma_find(uintptr_t virt_addr);
void            vma_unregister(uintptr_t virt_addr);
#endif // __MEMORY_VM_MANAGER_H__
