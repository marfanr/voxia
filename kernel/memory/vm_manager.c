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

static struct virtual_memory_page* create_vmm_page() {
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
	serial_trace("ok\n");
}

void vma_register(struct virtual_memory_page* page, uintptr_t phys_address, uintptr_t virt_addr, size_t size) {
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

virtual_memory_t* vma_find(uintptr_t virt_addr) {
	auto page = kernel_vmm_page;
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
		                        (uintptr_t)0x1000 * size);
		result = (uintptr_t)region;
	} else {
		uintptr_t next_addr = curr->end_address;
		vma_tree_add_locked(page, region, next_addr,
		                    next_addr + (uintptr_t)0x1000 * size);
		result = next_addr;
	}

	spin_release(&page->lock);
	return result;
}

struct virtual_memory_page* get_kernel_vmm_page() { return kernel_vmm_page; }

#undef RBT_ID_NAME
#undef RBT_TYPE