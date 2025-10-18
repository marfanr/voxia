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

typedef enum
{
    VMA_REGION_A = 0xFFFFA00000000000U,
    VMA_REGION_B = 0xFFFFB00000000000U,
    VMA_REGION_C = 0xFFFFC00000000000U,
} __attribute__((enum_extensibility(closed))) mem_vma_region;

typedef struct virtual_memory virtual_memory;
struct virtual_memory
{
    uintptr_t start_address;
    uintptr_t end_address;
    uintptr_t phys_address;
    size_t    length;
    int       flags;
    int       core;
} __attribute__((aligned(8)));

struct virtual_memory_tree_node
{
    struct virtual_memory_tree_node *parent;
    uintptr_t                        start_address;
    uintptr_t                        end_address;
    struct virtual_memory_tree_node *next;
} __attribute__((aligned(8)));

struct virtual_memory_tree
{
    struct virtual_memory_tree_node *active;
    struct virtual_memory_tree_node *unused;
};

void            vma_register(uintptr_t phys_address, uintptr_t virt_addr, size_t size);
virtual_memory *vma_find(uintptr_t virt_addr);
void            vma_unregister(uintptr_t virt_addr);
void            vma_tree_add(mem_vma_region region, uintptr_t start_address, uintptr_t end_address);
uintptr_t       vma_lookup_free_vaddr(mem_vma_region region, size_t size);
#endif // __MEMORY_VM_MANAGER_H__
