#include "autoconf.h"
#include "init/init.h"
#include <hal/cpu/paging.h>
#include <libk/serial.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/slab.h>
#include <memory/vm_manager.h>
#include <spinlock.h>
#include <str.h>
#include <type.h>

#define RBT_TYPE virtual_memory_t
#define RBT_ID_NAME start_address
#include <libk/tree/rbt.h>

#define PERCPU __attribute__((section(".data.percpu")))

extern boolean_t paging_has_been_set;

static rbt_node* VMA_RBT_NIL = 0;

static struct virtual_memory_page* kernel_vmm_page = 0;

static struct slab_cache* rbt_node_cache = NULL;
static struct slab_cache* vma_cache = NULL;
static struct slab_cache* vma_tree_zone_cache = NULL;
static struct slab_cache* vma_block_cache = NULL;
static struct slab_cache* vma_page = NULL;

static void vma_tree_add_locked(struct virtual_memory_page* page,
                                mem_vma_region region, uintptr_t start_address,
                                uintptr_t end_address) {
	struct virtual_memory_tree_node* node =
	    (struct virtual_memory_tree_node*)vxSlabAlloc(vma_tree_zone_cache);
	memset(node, 0, sizeof(struct virtual_memory_tree_node));
	node->start_address = start_address;
	node->end_address = end_address;
	node->next = 0;

	switch ((uintptr_t)region) {
	case VMA_REGION_A: {
		node->next = page->vma_tree_zone_a.active;
		page->vma_tree_zone_a.active = node;
		break;
	}
	case VMA_REGION_B: {
		node->next = page->vma_tree_zone_b.active;
		page->vma_tree_zone_b.active = node;
		break;
	}
	case VMA_REGION_C: {
		node->next = page->vma_tree_zone_c.active;
		page->vma_tree_zone_c.active = node;
		break;
	}
	case VMA_REGION_KMODULE: {
		node->next = page->vma_tree_zone_kmodule.active;
		page->vma_tree_zone_kmodule.active = node;
		break;
	}
	case VMA_REGION_PROCESS: {
		node->next = page->vma_tree_zone_process.active;
		page->vma_tree_zone_process.active = node;
		break;
	}
	default:
		slab_free(vma_tree_zone_cache, node);
		break;
	}
}

struct virtual_memory_page* create_vmm_page() {
	auto p = (struct virtual_memory_page*)vxSlabAlloc(vma_page);
	memset(p, 0, sizeof(struct virtual_memory_page));
	p->tree = VMA_RBT_NIL;
	return p;
}

INIT(vma) {
	vxCreateSlabCache(&rbt_node_cache, "rbt_node", sizeof(rbt_node), 64, 0);
	vxCreateSlabCache(&vma_cache, "vma", sizeof(virtual_memory_t), 64, 0);
	vxCreateSlabCache(&vma_tree_zone_cache, "vma_tree_zone",
	                  sizeof(struct virtual_memory_tree_node), 64, 0);
	vxCreateSlabCache(&vma_block_cache, "vma_block",
	                  sizeof(virtual_memory_block_t), 64, 0);
	vxCreateSlabCache(&vma_page, "vma_page",
	                  sizeof(struct virtual_memory_page), 64, 0);

	VMA_RBT_NIL = (rbt_node*)vxSlabAlloc(rbt_node_cache);
	VMA_RBT_NIL->data = (virtual_memory_t*)vxSlabAlloc(vma_cache);
	VMA_RBT_NIL->data->start_address = 0;
	VMA_RBT_NIL->data->end_address = 0;
	VMA_RBT_NIL->left = VMA_RBT_NIL->right = VMA_RBT_NIL->parent =
	    VMA_RBT_NIL;

	kernel_vmm_page = create_vmm_page();
}

void vma_register(struct virtual_memory_page* page, uintptr_t phys_address,
                  uintptr_t virt_addr, size_t size) {
	spin_acquire(&page->lock);
	virtual_memory_t* node = (virtual_memory_t*)vxSlabAlloc(vma_cache);
	node->start_address = virt_addr;
	node->end_address = virt_addr + size;
	node->phys_address = phys_address;
	node->length = size;
	node->flags = 0;
	node->core = 0;

	rbt_node* n = (rbt_node*)vxSlabAlloc(rbt_node_cache);
	rbt_insert_node(&page->tree, n, node, VMA_RBT_NIL);
	spin_release(&page->lock);
}

virtual_memory_t* vma_find(struct virtual_memory_page* page, uintptr_t virt_addr) {
	spin_acquire(&page->lock);
	struct rbt_node* n =
	    rbt_search_node(page->tree, virt_addr, VMA_RBT_NIL);
	virtual_memory_t* ret = (n != VMA_RBT_NIL) ? n->data : NULL;
	spin_release(&page->lock);
	return ret;
}

void vma_unregister(struct virtual_memory_page* page, uintptr_t virt_addr) {
	spin_acquire(&page->lock);
	struct rbt_node* n =
	    rbt_search_node(page->tree, virt_addr, VMA_RBT_NIL);
	if (n == VMA_RBT_NIL) {
		spin_release(&page->lock);
		return;
	}
	rbt_remove_node(&page->tree, n, VMA_RBT_NIL);
	spin_release(&page->lock);
}

static void vma_rbt_debug_node(rbt_node* node, int level) {
	if (node == VMA_RBT_NIL || node == NULL)
		return;

	vma_rbt_debug_node(node->left, level + 1);

	for (int i = 0; i < level; i++)
		serial_trace("      ");
	serial_trace("start 0x%x - 0x%x\n", node->data->start_address,
	             node->data->end_address);

	vma_rbt_debug_node(node->right, level + 1);
}

__attribute__((unused)) static void vma_rbt_debug(rbt_node* root) {
	vma_rbt_debug_node(root, 0);
}

uintptr_t vma_lookup_free_vaddr(struct virtual_memory_page* page,
                                mem_vma_region region, size_t size) {
	spin_acquire(&page->lock);

	struct virtual_memory_tree_node* curr = NULL;

	switch (region) {
	case VMA_REGION_A:
		curr = page->vma_tree_zone_a.active;
		break;
	case VMA_REGION_B:
		curr = page->vma_tree_zone_b.active;
		break;
	case VMA_REGION_C:
		curr = page->vma_tree_zone_c.active;
		break;
	case VMA_REGION_KMODULE:
		curr = page->vma_tree_zone_kmodule.active;
		break;
	case VMA_REGION_PROCESS:
		curr = page->vma_tree_zone_process.active;
		break;
	default:
		spin_release(&page->lock);
		return 0;
	}

	uintptr_t result;

	if (curr == NULL) {
		vma_tree_add_locked(page, region, (uintptr_t)region,
		                    (uintptr_t)region +
		                        (uintptr_t)BLOCK_SIZE * size);
		result = (uintptr_t)region;
	} else {
		uintptr_t next_addr = curr->end_address;
		vma_tree_add_locked(page, region, next_addr,
		                    next_addr + (uintptr_t)BLOCK_SIZE * size);
		result = next_addr;
	}

	spin_release(&page->lock);
	return result;
}
__attribute__((always_inline))
struct virtual_memory_page* get_kernel_vmm_page() { return kernel_vmm_page; }

static void vma_mmap_recursive(struct rbt_node* node, uintptr_t* pml4) {
	if (node == VMA_RBT_NIL)
		return;

	vma_mmap_recursive(node->left, pml4);

	virtual_memory_t* vma = node->data;

	uintptr_t size = vma->end_address - vma->start_address;
	uintptr_t pages = (size + 4095) / BLOCK_SIZE; // round up
	if (pages == 0)
		pages = 1;

	vxMultipleMmap(pml4, vma->start_address, vma->phys_address, pages,
	               0b111);

	vma_mmap_recursive(node->right, pml4);
}

void vma_mmap(struct virtual_memory_page* vmapage, uintptr_t* pml4) {
	spin_acquire(&vmapage->lock);
	vma_mmap_recursive(vmapage->tree, pml4);
	spin_release(&vmapage->lock);
}

static void vma_tree_clone(struct virtual_memory_tree* dest, const struct virtual_memory_tree* src) {
	dest->unused = NULL;
	dest->active = NULL;

	struct virtual_memory_tree_node* src_node = src->active;
	struct virtual_memory_tree_node* last_dest_node = NULL;

	while (src_node) {
		struct virtual_memory_tree_node* new_node = 
			(struct virtual_memory_tree_node*)vxSlabAlloc(vma_tree_zone_cache);
		memset(new_node, 0, sizeof(struct virtual_memory_tree_node));
		new_node->start_address = src_node->start_address;
		new_node->end_address = src_node->end_address;
		new_node->next = NULL;

		if (last_dest_node == NULL) {
			dest->active = new_node;
		} else {
			last_dest_node->next = new_node;
		}
		last_dest_node = new_node;
		src_node = src_node->next;
	}
}

static int vma_clone_cow_recursive(struct rbt_node* node, struct virtual_memory_page* child_vmapage, uintptr_t* child_pml4, uintptr_t* parent_pml4) {
	if (node == VMA_RBT_NIL)
		return 0;

	int err = vma_clone_cow_recursive(node->left, child_vmapage, child_pml4, parent_pml4);
	if (err < 0)
		return err;

	virtual_memory_t* vma = node->data;

	if (vma->start_address < KERNEL_BASE) {
		uintptr_t size = vma->end_address - vma->start_address;
		uintptr_t pages = (size + 4095) / BLOCK_SIZE;
		if (pages == 0) pages = 1;

		for (uintptr_t i = 0; i < pages; i++) {
			uintptr_t virt = vma->start_address + i * BLOCK_SIZE;
			uint64_t entry = paging_get_entry(parent_pml4, virt);
			if (entry & 1) {
				// Mark parent's entry as Read-Only
				paging_make_cow(parent_pml4, virt);
				
				uintptr_t phys = entry & PAGE_PHYS_MASK;
				
				// Map in child's PML4 as COW (0x205)
				// 0x200 (COW) | 0x4 (User) | 0x1 (Present) = 0x205
				vxMmap(child_pml4, virt, phys, 0x205);
			}
		}

		vma_register(child_vmapage, vma->phys_address, vma->start_address, size);
	} else {
		uintptr_t size = vma->end_address - vma->start_address;
		uintptr_t pages = (size + 4095) / BLOCK_SIZE;
		if (pages == 0) pages = 1;

		vxMultipleMmap(child_pml4, vma->start_address, vma->phys_address, pages, 0b111);
		vma_register(child_vmapage, vma->phys_address, vma->start_address, size);
	}

	return vma_clone_cow_recursive(node->right, child_vmapage, child_pml4, parent_pml4);
}

int vma_clone_cow(struct virtual_memory_page* parent_vmapage, struct virtual_memory_page* child_vmapage, uintptr_t* child_pml4, uintptr_t* parent_pml4) {
	spin_acquire(&parent_vmapage->lock);
	spin_acquire(&child_vmapage->lock);

	vma_tree_clone(&child_vmapage->vma_tree_zone_process, &parent_vmapage->vma_tree_zone_process);
	vma_tree_clone(&child_vmapage->vma_tree_zone_a, &parent_vmapage->vma_tree_zone_a);
	vma_tree_clone(&child_vmapage->vma_tree_zone_b, &parent_vmapage->vma_tree_zone_b);
	vma_tree_clone(&child_vmapage->vma_tree_zone_c, &parent_vmapage->vma_tree_zone_c);
	vma_tree_clone(&child_vmapage->vma_tree_zone_kmodule, &parent_vmapage->vma_tree_zone_kmodule);

	spin_release(&child_vmapage->lock);
	spin_release(&parent_vmapage->lock);

	return vma_clone_cow_recursive(parent_vmapage->tree, child_vmapage, child_pml4, parent_pml4);
}

#undef RBT_ID_NAME
#undef RBT_TYPE