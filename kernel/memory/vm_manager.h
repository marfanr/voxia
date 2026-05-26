#ifndef __MEMORY_VM_MANAGER_H__
#define __MEMORY_VM_MANAGER_H__

#include <spinlock.h>
#include <type.h>

#define KERNEL_BASE 0xFFFF800000000000ULL
#define REGION_SIZE 0x0000008000000000ULL // 512 GB
#define USER_STACK_VADDR 0x7FFFFFFFE000ULL
#define USER_MMAP_BASE 0x100000000ULL

typedef enum : uintptr_t {
	VMA_REGION_A = KERNEL_BASE,                     // 0xFFFF800000000000
	VMA_REGION_B = KERNEL_BASE + (REGION_SIZE * 1), // 0xFFFF808000000000
	VMA_REGION_C = KERNEL_BASE + (REGION_SIZE * 2), // 0xFFFF810000000000
	VMA_REGION_KMODULE =
	    KERNEL_BASE + (REGION_SIZE * 3), // 0xFFFF818000000000
	KALLOC_BASE_ADDR =
	    KERNEL_BASE + (REGION_SIZE * 4), // 0xFFFF820000000000

	// Physical window bebas diletakkan jauh dari region di atas
	mem_vma_phys_window_pt = 0xFFFFB00000000000ULL,
	mem_vma_phys_window_start = 0xFFFFD00000000000ULL,

	VMA_REGION_PROCESS = 0x400000,
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
