#ifndef __MEMORY_VM_MANAGER_H__
#define __MEMORY_VM_MANAGER_H__

#include <hal/cpu/spinlock.h>
#include <type.h>

enum mem_vma_base {
	mem_vma_phys_window_start = 0xFFFFD00000000000,
	mem_vma_phys_window_pt = 0xFFFFB0000F000000,
} __attribute__((enum_extensibility(closed)));

typedef enum : uintptr_t {
	VMA_REGION_A = 0xFFFFA00000000000U,
	VMA_REGION_B = 0xFFFFB00000000000U,
	// for PCI
	VMA_REGION_C = 0xFFFFC00000000000U,
	// for MODULE
	VMA_REGION_KMODULE = 0x400000,
	VMA_REGION_KLIBRARY = 0x80000000,
} mem_vma_region;

typedef struct virtual_memory_block virtual_memory_block_t;
struct virtual_memory_block {
	uintptr_t phys_address;
	uintptr_t pml4_base;
	struct virtual_memory_block* next;
};

typedef struct virtual_memory virtual_memory_t;
struct virtual_memory {
	uintptr_t start_address;
	uintptr_t end_address;
	// virtual_memory_block_t *block;
	uintptr_t phys_address;
	size_t length;
	int flags;
	int core;
} __attribute__((aligned(64)));

struct virtual_memory_tree_node {
	struct virtual_memory_tree_node* parent;
	uintptr_t start_address;
	uintptr_t end_address;
	struct virtual_memory_tree_node* next;
} __attribute__((aligned(8)));

void vma_register(uintptr_t phys_address, uintptr_t virt_addr, size_t size);
virtual_memory_t* vma_find(uintptr_t virt_addr);
void vma_unregister(uintptr_t virt_addr);
void vma_tree_add(mem_vma_region region, uintptr_t start_address,
		  uintptr_t end_address);
uintptr_t vma_lookup_free_vaddr(mem_vma_region region, size_t size);
#endif // __MEMORY_VM_MANAGER_H__
