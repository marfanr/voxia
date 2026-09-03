#ifndef __MEMORY_VM_MANAGER_H__
#define __MEMORY_VM_MANAGER_H__

#include "hal/cpu/paging.h"
#include <memory/memory_utils.h>
#include <spinlock.h>
#include <type.h>

#define REGION_SIZE 0x0000000400000000ULL // 16 GB (lebih aman dari overflow)
#define USER_STACK_VADDR 0x7FFFFFFFE000ULL
#define USER_MMAP_BASE 0x100000000ULL

typedef uintptr_t mem_vma_region;

#define VMA_ANCHOR 0xffffff8000000000ULL
#define VMA_REGION_A (VMA_ANCHOR)
#define VMA_REGION_B (VMA_REGION_A + REGION_SIZE)
#define VMA_REGION_C (VMA_REGION_B + REGION_SIZE)
#define VMA_REGION_KMODULE (VMA_REGION_C + REGION_SIZE)
#define VMA_REGION_SLAB (VMA_REGION_KMODULE + REGION_SIZE)
#define VMA_REGION_KALLOC (VMA_REGION_SLAB + REGION_SIZE)
#define KALLOC_BASE_ADDR (VMA_REGION_KALLOC)

// Physical window bebas diletakkan jauh dari region di atas
#define mem_vma_phys_window_start 0xFFFFD00000000000ULL
#define mem_vma_phys_window_pt (mem_vma_phys_window_start + GB)

#define VMA_REGION_PROCESS 0x400000

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
	uint64_t flags;
	int core;
} __attribute__((aligned(64)));

struct virtual_memory_tree_node {
	struct virtual_memory_tree_node* parent;
	uintptr_t start_address;
	uintptr_t end_address;
	struct virtual_memory_tree_node* next;
} __attribute__((aligned(8)));

struct virtual_memory_tree {
	struct virtual_memory_tree_node* active;
	struct virtual_memory_tree_node* unused;
};

struct rbt_node;
struct virtual_memory_page {
	spinlock_t lock;
	struct virtual_memory_tree vma_tree_zone_process;
	struct virtual_memory_tree vma_tree_zone_a;
	struct rbt_node* tree;
	uint8_t _pad[6];
	struct virtual_memory_tree vma_tree_zone_b;
	struct virtual_memory_tree vma_tree_zone_c;
	struct virtual_memory_tree vma_tree_zone_kmodule;
	struct virtual_memory_tree vma_tree_zone_user_mmap;
} __attribute__((aligned(64)));

void vma_register(struct virtual_memory_page* page, uintptr_t phys_address,
                  uintptr_t virt_addr, size_t size, uint64_t flags);
virtual_memory_t* vma_find(struct virtual_memory_page* page,
                           uintptr_t virt_addr);
void vma_unregister(struct virtual_memory_page* page, uintptr_t virt_addr);
uintptr_t vma_lookup_free_vaddr(struct virtual_memory_page* page,
                                mem_vma_region region, size_t size);
struct virtual_memory_page* get_kernel_vmm_page();
struct virtual_memory_page* create_vmm_page();
void vma_mmap(struct virtual_memory_page* vmapage, uintptr_t* pml4);
int vma_clone_cow(struct virtual_memory_page* parent_vmapage,
                  struct virtual_memory_page* child_vmapage,
                  uintptr_t* child_pml4, uintptr_t* parent_pml4);

void vma_unmap_all(struct virtual_memory_page* page, uintptr_t* pml4);

virtual_memory_t* vma_find_contains(struct virtual_memory_page* page,
                                    uintptr_t virt_addr);

#endif // __MEMORY_VM_MANAGER_H__
