#include "autoconf.h"
#include "hal/cpu/core.h"
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

static void vma_tree_add_locked(struct virtual_memory_page* page, mem_vma_region region, uintptr_t start_address, uintptr_t end_address) {
	struct virtual_memory_tree_node* node = (struct virtual_memory_tree_node*)vxSlabAlloc(vma_tree_zone_cache);
	memset(node, 0, sizeof(struct virtual_memory_tree_node));
	node->start_address = start_address;
	node->end_address = end_address;
	node->next = 0;

	if (region == VMA_REGION_A) {
		node->next = page->vma_tree_zone_a.active;
		page->vma_tree_zone_a.active = node;
	} else if (region == VMA_REGION_B) {
		node->next = page->vma_tree_zone_b.active;
		page->vma_tree_zone_b.active = node;
	} else if (region == VMA_REGION_C) {
		node->next = page->vma_tree_zone_c.active;
		page->vma_tree_zone_c.active = node;
	} else if (region == VMA_REGION_KMODULE) {
		node->next = page->vma_tree_zone_kmodule.active;
		page->vma_tree_zone_kmodule.active = node;
	} else if (region == VMA_REGION_PROCESS) {
		node->next = page->vma_tree_zone_process.active;
		page->vma_tree_zone_process.active = node;
	} else if (region == USER_MMAP_BASE) {
		node->next = page->vma_tree_zone_user_mmap.active;
		page->vma_tree_zone_user_mmap.active = node;
	} else {
		slab_free(vma_tree_zone_cache, node);
	}
}

struct virtual_memory_page* create_vmm_page() {
	auto p = (struct virtual_memory_page*)vxSlabAlloc(vma_page);
	memset(p, 0, sizeof(struct virtual_memory_page));
	p->tree = VMA_RBT_NIL;
	p->lock.next_ticket = p->lock.now_serving = 0;
	return p;
}

INIT(vma) {
	vxCreateSlabCache(&rbt_node_cache, "rbt_node", sizeof(rbt_node), 64, 0);
	vxCreateSlabCache(&vma_cache, "vma", sizeof(virtual_memory_t), 64, 0);
	vxCreateSlabCache(&vma_tree_zone_cache, "vma_tree_zone", sizeof(struct virtual_memory_tree_node), 64, 0);
	vxCreateSlabCache(&vma_block_cache, "vma_block", sizeof(virtual_memory_block_t), 64, 0);
	vxCreateSlabCache(&vma_page, "vma_page", sizeof(struct virtual_memory_page), 64, 0);

	VMA_RBT_NIL = (rbt_node*)vxSlabAlloc(rbt_node_cache);
	memset(VMA_RBT_NIL, 0, sizeof(rbt_node));
	VMA_RBT_NIL->color = RBT_BLACK;
	VMA_RBT_NIL->left = VMA_RBT_NIL->right = VMA_RBT_NIL->parent = VMA_RBT_NIL;

	kernel_vmm_page = create_vmm_page();
}

void vma_register_locked(struct virtual_memory_page* page, uintptr_t phys_address, uintptr_t virt_addr, size_t size, uint64_t flags);
void vma_register_locked(struct virtual_memory_page* page, uintptr_t phys_address, uintptr_t virt_addr, size_t size, uint64_t flags) {
	if (!page)
		return;

	rbt_node* existing = rbt_search_node(page->tree, virt_addr, VMA_RBT_NIL);
	if (existing != VMA_RBT_NIL) {
		return;
	}

	virtual_memory_t* node = (virtual_memory_t*)vxSlabAlloc(vma_cache);
	memset(node, 0, sizeof(virtual_memory_t));
	node->start_address = virt_addr;
	node->end_address = virt_addr + size;
	node->phys_address = phys_address;
	node->length = size;
	node->flags = flags;
	node->core = 0;

	rbt_node* n = (rbt_node*)vxSlabAlloc(rbt_node_cache);
	rbt_insert_node(&page->tree, n, node, VMA_RBT_NIL);
}

void vma_register(struct virtual_memory_page* page, uintptr_t phys_address, uintptr_t virt_addr, size_t size, uint64_t flags) {
	if (!page)
		return;
	spin_acquire(&page->lock);
	vma_register_locked(page, phys_address, virt_addr, size, flags);
	spin_release(&page->lock);
}

virtual_memory_t* vma_find(struct virtual_memory_page* page, uintptr_t virt_addr) {
	spin_acquire(&page->lock);
	struct rbt_node* n = rbt_search_node(page->tree, virt_addr, VMA_RBT_NIL);
	virtual_memory_t* ret = (n != VMA_RBT_NIL) ? n->data : NULL;
	spin_release(&page->lock);
	return ret;
}

virtual_memory_t* vma_find_contains(struct virtual_memory_page* page, uintptr_t virt_addr) {
	if (!page)
		return NULL;

	spin_acquire(&page->lock);
	struct rbt_node* curr = page->tree;
	virtual_memory_t* found = NULL;

	while (curr && curr != VMA_RBT_NIL) {
		virtual_memory_t* vma = (virtual_memory_t*)curr->data;
		if (!vma)
			break;

		if (virt_addr >= vma->start_address && virt_addr < vma->end_address) {
			found = vma;
			break;
		} else if (virt_addr < vma->start_address) {
			curr = curr->left;
		} else {
			curr = curr->right;
		}
	}

	spin_release(&page->lock);
	return found;
}

void vma_unregister(struct virtual_memory_page* page, uintptr_t virt_addr) {
	spin_acquire(&page->lock);
	struct rbt_node* n = rbt_search_node(page->tree, virt_addr, VMA_RBT_NIL);
	if (n == VMA_RBT_NIL) {
		spin_release(&page->lock);
		return;
	}
	virtual_memory_t* vma = n->data;
	rbt_remove_node(&page->tree, n, VMA_RBT_NIL);
	slab_free(vma_cache, vma);
	slab_free(rbt_node_cache, n);
	spin_release(&page->lock);
}

uintptr_t vma_lookup_free_vaddr(struct virtual_memory_page* page, mem_vma_region region, size_t size) {
	if (!page)
		return 0;
	spin_acquire(&page->lock);

	struct virtual_memory_tree_node* curr = NULL;

	if (region == VMA_REGION_A) {
		curr = page->vma_tree_zone_a.active;
	} else if (region == VMA_REGION_B) {
		curr = page->vma_tree_zone_b.active;
	} else if (region == VMA_REGION_C) {
		curr = page->vma_tree_zone_c.active;
	} else if (region == VMA_REGION_KMODULE) {
		curr = page->vma_tree_zone_kmodule.active;
	} else if (region == VMA_REGION_PROCESS) {
		curr = page->vma_tree_zone_process.active;
	} else if (region == USER_MMAP_BASE) {
		curr = page->vma_tree_zone_user_mmap.active;
	} else {
		spin_release(&page->lock);
		return 0;
	}

	uintptr_t result;
	if (curr == NULL) {
		result = (uintptr_t)region;
	} else {
		result = curr->end_address;
	}

	while (1) {
		bool collision = false;
		// Check if any address in [result, result + BLOCK_SIZE * size - 1] is mapped
		// Since we don't have a full range check, we can just check the start and end,
		// or iterate through the RBT tree. For simplicity, check every page.
		for (size_t i = 0; i < size; i++) {
			uintptr_t check_addr = result + i * BLOCK_SIZE;
			
			struct rbt_node* curr_node = page->tree;
			virtual_memory_t* v = NULL;
			while (curr_node && curr_node != VMA_RBT_NIL) {
				virtual_memory_t* vma = (virtual_memory_t*)curr_node->data;
				if (!vma) break;
				if (check_addr >= vma->start_address && check_addr < vma->end_address) {
					v = vma;
					break;
				} else if (check_addr < vma->start_address) {
					curr_node = curr_node->left;
				} else {
					curr_node = curr_node->right;
				}
			}

			if (v != NULL) {
				collision = true;
				result = v->end_address;
				break;
			}
		}
		if (!collision) {
			break;
		}
	}

	vma_tree_add_locked(page, region, result, result + (uintptr_t)BLOCK_SIZE * size);

	spin_release(&page->lock);
	return result;
}

struct virtual_memory_page* get_kernel_vmm_page() { return kernel_vmm_page; }

static void vma_mmap_recursive(struct rbt_node* node, uintptr_t* pml4) {
	if (node == VMA_RBT_NIL || node == NULL)
		return;

	vma_mmap_recursive(node->left, pml4);

	virtual_memory_t* vma = node->data;
	if (vma) {
		uintptr_t size = vma->end_address - vma->start_address;
		uintptr_t pages = (size + 4095) / BLOCK_SIZE;
		if (pages == 0)
			pages = 1;
		paging_multiple_mmap(pml4, vma->start_address, vma->phys_address, pages, (uint64_t)vma->flags);
	}

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
		struct virtual_memory_tree_node* new_node = (struct virtual_memory_tree_node*)vxSlabAlloc(vma_tree_zone_cache);
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
	if (node == VMA_RBT_NIL || node == NULL) {
		return 0;
	}

	int err = vma_clone_cow_recursive(node->left, child_vmapage, child_pml4, parent_pml4);
	if (err < 0)
		return err;

	virtual_memory_t* vma = node->data;
	if (vma && vma->start_address < vma->end_address) {
		uintptr_t size = vma->end_address - vma->start_address;
		uintptr_t pages = (size + 4095) / BLOCK_SIZE;
		if (pages == 0)
			pages = 1;

		// Sanity check: prevent huge/random page counts
		if (pages < 1000000) {
			if (vma->start_address < VMA_REGION_A) {
				for (uintptr_t i = 0; i < pages; i++) {
					uintptr_t virt = vma->start_address + i * BLOCK_SIZE;
					uint64_t entry = paging_get_entry(parent_pml4, virt);
					if (entry & 1) {
						uint64_t pre_cow_entry = entry;
						paging_make_cow(parent_pml4, virt);
						uintptr_t phys = vaddr_to_paddr(parent_pml4, virt);
						uint64_t new_entry = paging_get_entry(parent_pml4, virt);
						/* Debug: log pages near the faulting address */
						uint64_t child_flags = PAGE_PRESENT | PAGE_USER ;
						if (virt >= 0x100020000ULL && virt <= 0x100025000ULL) {
							auto thr = get_current_core_data()->active_thread;
							serial2_printf("[COW-CLONE] virt=0x%lx pre=0x%lx post=0x%lx phys=0x%lx (cow %d) (thr id %d)\n", virt, pre_cow_entry,
							               new_entry, phys, new_entry & PAGE_COW, thr ? thr->id : 0);
						}
						if (new_entry & PAGE_COW)
							child_flags |= PAGE_COW;
						if (new_entry & PAGE_NO_EXECUTE)
							child_flags |= PAGE_NO_EXECUTE;
						if (new_entry & PAGE_WRITABLE)
							child_flags |= PAGE_WRITABLE;

						if (virt >= 0x100020000ULL && virt <= 0x100025000ULL)
							serial2_printf("[COW] child flags %b\n", child_flags);
						
						paging_mmap(child_pml4, virt, phys, child_flags);
					}
				}
			} else {
				paging_multiple_mmap(child_pml4, vma->start_address, vma->phys_address, pages, vma->flags);
			}
			vma_register_locked(child_vmapage, vma->phys_address, vma->start_address, size, vma->flags);
		}
	}

	return vma_clone_cow_recursive(node->right, child_vmapage, child_pml4, parent_pml4);
}

int vma_clone_cow(struct virtual_memory_page* parent_vmapage, struct virtual_memory_page* child_vmapage, uintptr_t* child_pml4, uintptr_t* parent_pml4) {

	if (parent_vmapage == child_vmapage)
		return -1;

	spin_acquire(&parent_vmapage->lock);
	spin_acquire(&child_vmapage->lock);

	vma_tree_clone(&child_vmapage->vma_tree_zone_process, &parent_vmapage->vma_tree_zone_process);
	vma_tree_clone(&child_vmapage->vma_tree_zone_a, &parent_vmapage->vma_tree_zone_a);
	vma_tree_clone(&child_vmapage->vma_tree_zone_b, &parent_vmapage->vma_tree_zone_b);
	vma_tree_clone(&child_vmapage->vma_tree_zone_c, &parent_vmapage->vma_tree_zone_c);
	vma_tree_clone(&child_vmapage->vma_tree_zone_kmodule, &parent_vmapage->vma_tree_zone_kmodule);

	int ret = vma_clone_cow_recursive(parent_vmapage->tree, child_vmapage, child_pml4, parent_pml4);

	spin_release(&child_vmapage->lock);
	spin_release(&parent_vmapage->lock);

	return ret;
}

static void vma_unmap_all_recursive(rbt_node* node, uintptr_t* pml4) {
	if (node == VMA_RBT_NIL || node == NULL)
		return;

	vma_unmap_all_recursive(node->left, pml4);
	vma_unmap_all_recursive(node->right, pml4);

	virtual_memory_t* vma = node->data;
	if (vma) {
		uintptr_t size = vma->end_address - vma->start_address;
		uintptr_t pages = (size + 4095) / BLOCK_SIZE;
		if (pages == 0)
			pages = 1;

		if (pml4) {
			paging_multiple_unmap(pml4, vma->start_address, pages);
		}
		slab_free(vma_cache, vma);
	}
	slab_free(rbt_node_cache, node);
}

static void vma_tree_zone_free(struct virtual_memory_tree* tree) {
	struct virtual_memory_tree_node* curr = tree->active;
	while (curr) {
		struct virtual_memory_tree_node* next = curr->next;
		slab_free(vma_tree_zone_cache, curr);
		curr = next;
	}
	tree->active = NULL;

	curr = tree->unused;
	while (curr) {
		struct virtual_memory_tree_node* next = curr->next;
		slab_free(vma_tree_zone_cache, curr);
		curr = next;
	}
	tree->unused = NULL;
}

void vma_unmap_all(struct virtual_memory_page* page, uintptr_t* pml4) {
	spin_acquire(&page->lock);

	vma_unmap_all_recursive(page->tree, pml4);
	page->tree = VMA_RBT_NIL;

	vma_tree_zone_free(&page->vma_tree_zone_process);
	vma_tree_zone_free(&page->vma_tree_zone_a);
	vma_tree_zone_free(&page->vma_tree_zone_b);
	vma_tree_zone_free(&page->vma_tree_zone_c);
	vma_tree_zone_free(&page->vma_tree_zone_kmodule);

	spin_release(&page->lock);
}

#undef RBT_ID_NAME
#undef RBT_TYPE
