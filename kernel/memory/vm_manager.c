#include "autoconf.h"
#include "init/init.h"
#include <type.h>
#include <hal/cpu/paging.h>
#include <spinlock.h>
#include <libk/serial.h>
#include <str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/slab.h>
#include <memory/vm_manager.h>

#define RBT_TYPE virtual_memory_t
#define RBT_ID_NAME start_address
#include <libk/tree/rbt.h>

#define PERCPU __attribute__((section(".data.percpu")))

// extern variables
extern boolean_t paging_has_been_set;

static rbt_node* VMA_RBT_NIL = 0;
static rbt_node* virtual_memory_tree_root = 0;

struct virtual_memory_tree {
	struct virtual_memory_tree_node* active;
	struct virtual_memory_tree_node* unused;
};

// hnya unutk kebutuhan lookup zone
static struct virtual_memory_tree vma_tree_zone_a = {0};
static struct virtual_memory_tree vma_tree_zone_b = {0};
static struct virtual_memory_tree vma_tree_zone_c = {0};
static struct virtual_memory_tree vma_tree_zone_kmodule = {0};

static struct slab_cache* rbt_node_cache = NULL;
static struct slab_cache* vma_cache = NULL;
static struct slab_cache* vma_tree_zone_cache = NULL;
static struct slab_cache* vma_block_cache = NULL;

static spinlock_t vma_lock = {0};

INIT(vma) {
	LOG_DEBUG("VMA", "zone_a active %x, unused %x",
		  (void*) vma_tree_zone_a.active,
		  (void*) vma_tree_zone_a.unused);
	LOG_DEBUG("VMA", "zone_b active %x, unused %x",
		  (void*) vma_tree_zone_b.active,
		  (void*) vma_tree_zone_b.unused);
	LOG_DEBUG("VMA", "zone_c active %x, unused %x",
		  (void*) vma_tree_zone_c.active,
		  (void*) vma_tree_zone_c.unused);

	vxCreateSlabCache(&rbt_node_cache, "rbt_node", sizeof(rbt_node), 64, 0);
	vxCreateSlabCache(&vma_cache, "vma", sizeof(virtual_memory_t), 64, 0);
	vxCreateSlabCache(&vma_tree_zone_cache, "vma_tree_zone",
			  sizeof(struct virtual_memory_tree_node), 64, 0);
	vxCreateSlabCache(&vma_block_cache, "vma_block",
			  sizeof(virtual_memory_block_t), 64, 0);

	VMA_RBT_NIL = (rbt_node*) vxSlabAlloc(rbt_node_cache);
	VMA_RBT_NIL->data = (virtual_memory_t*) vxSlabAlloc(vma_cache);
	VMA_RBT_NIL->data->start_address = 0;
	VMA_RBT_NIL->data->end_address = 0;
	VMA_RBT_NIL->left = VMA_RBT_NIL->right = VMA_RBT_NIL->parent =
		VMA_RBT_NIL;
	virtual_memory_tree_root = VMA_RBT_NIL;

	vma_tree_zone_a.active = 0;
	vma_tree_zone_b.active = 0;
	vma_tree_zone_c.active = 0;
	vma_tree_zone_a.unused = 0;
	vma_tree_zone_b.unused = 0;
	vma_tree_zone_c.unused = 0;
	vma_tree_zone_kmodule.active = 0;
	vma_tree_zone_kmodule.unused = 0;
}

void vma_register(uintptr_t phys_address, uintptr_t virt_addr, size_t size) {
	spin_acquire(&vma_lock);
	virtual_memory_t* node = (virtual_memory_t*) vxSlabAlloc(vma_cache);
	node->start_address = virt_addr;
	node->end_address = virt_addr + size;
	node->phys_address = phys_address;
	node->length = size;
	node->flags = 0;
	node->core = 0;

	rbt_node* n = (rbt_node*) vxSlabAlloc(rbt_node_cache);
	rbt_insert_node(&virtual_memory_tree_root, n, node, VMA_RBT_NIL);
	spin_release(&vma_lock);
}

virtual_memory_t* vma_find(uintptr_t virt_addr) {
	spin_acquire(&vma_lock);
	struct rbt_node* n = rbt_search_node(virtual_memory_tree_root,
					     virt_addr, VMA_RBT_NIL);
	virtual_memory_t* ret = (n != VMA_RBT_NIL) ? n->data : NULL;
	spin_release(&vma_lock);
	return ret;
}

void vma_unregister(uintptr_t virt_addr) {
	spin_acquire(&vma_lock);
	struct rbt_node* n = rbt_search_node(virtual_memory_tree_root,
					     virt_addr, VMA_RBT_NIL);
	if (n == VMA_RBT_NIL) {
		spin_release(&vma_lock);
		return;
	}
	rbt_remove_node(&virtual_memory_tree_root, n, VMA_RBT_NIL);
	spin_release(&vma_lock);
}

static void vma_rbt_debug_node(rbt_node* node, int level) {
	if (node == VMA_RBT_NIL || node == NULL)
		return;

	// traverse left subtree
	vma_rbt_debug_node(node->left, level + 1);

	// print current node
	for (int i = 0; i < level; i++)
		serial_trace("      ");
	serial_trace("start 0x%x - 0x%x\n", node->data->start_address,
		     node->data->end_address);

	// traverse right subtree
	vma_rbt_debug_node(node->right, level + 1);
}

__attribute__((unused)) static void vma_rbt_debug(rbt_node* root) {
	vma_rbt_debug_node(root, 0);
}

void vma_tree_add(mem_vma_region region, uintptr_t start_address,
		  uintptr_t end_address) {
	struct virtual_memory_tree_node* node =
		(struct virtual_memory_tree_node*) vxSlabAlloc(
			vma_tree_zone_cache);
	memset(node, 0, sizeof(struct virtual_memory_tree_node));
	node->start_address = start_address;
	node->end_address = end_address;

	// LOG_DEBUG("vma", "node 0x%x  ", node);
	node->next = 0;

	switch ((uintptr_t) region) {
	case VMA_REGION_A: {
		node->next = vma_tree_zone_a.active;
		vma_tree_zone_a.active = node;
		break;
	}
	case VMA_REGION_B: {
		node->next = vma_tree_zone_b.active;
		vma_tree_zone_b.active = node;
		break;
	}
	case VMA_REGION_C: {
		node->next = vma_tree_zone_c.active;
		vma_tree_zone_c.active = node;
		break;
	}
	case VMA_REGION_KMODULE: {
		node->next = vma_tree_zone_kmodule.active;
		vma_tree_zone_kmodule.active = node;
		break;
	}
	}
}

uintptr_t vma_lookup_free_vaddr(mem_vma_region region, size_t size) {
	spin_acquire(&vma_lock);
	struct virtual_memory_tree_node* curr = 0;
	// LOG_DEBUG("VMA", "curr %x", curr);

	switch (region) {
	case VMA_REGION_A: {
		curr = vma_tree_zone_a.active;
		break;
	}
	case VMA_REGION_B: {
		curr = vma_tree_zone_b.active;
		break;
	}
	case VMA_REGION_C: {
		curr = vma_tree_zone_c.active;
		break;
	}
	case VMA_REGION_KMODULE: {
		curr = vma_tree_zone_kmodule.active;
		break;
	}
	case VMA_REGION_KLIBRARY:
	default:
		break;
	}

	if (curr == 0) {
		// LOG_DEBUG("VMA", "add 1st region 0x%x", (uintptr_t) region);
		vma_tree_add(region, (uintptr_t) region,
			     (uintptr_t) region + 0x1000 * size);
		spin_release(&vma_lock);
		return (uintptr_t) region;
	}
	// LOG_DEBUG("VMA", "zone %x", curr->start_address);
	// LOG_DEBUG("vma", "node 0x%x  ", curr->start_address);

	uintptr_t next_addr = curr->end_address;
	vma_tree_add(region, (uintptr_t) next_addr,
		     (uintptr_t) next_addr + 0x1000 * size);

	spin_release(&vma_lock);
	return next_addr;
}

#undef RBT_ID_NAME
#undef RBT_TYPE
