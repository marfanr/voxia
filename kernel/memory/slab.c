#include "slab.h"
#include <type.h>

#include <libk/serial.h>
#include <str.h>

#include <hal/cpu/paging.h>
#include <spinlock.h>
#include <str.h>
#include <memory/phys_base_allocator.h>
#include <memory/memory_utils.h>

#define MAX_FREED_VADDRS 512

#define DEFAULT_SLAB_ADDR 0xFFFFFF8080000000
static uintptr_t last_slab_addr = DEFAULT_SLAB_ADDR;

static spinlock_t slab_global_lock = {0};

static uintptr_t freed_vaddrs[MAX_FREED_VADDRS] = {0};
static size_t freed_vaddr_count = 0;

static uintptr_t get_default_slab_addr() {
	spin_acquire(&slab_global_lock);
	if (freed_vaddr_count > 0) {
		uintptr_t addr = freed_vaddrs[--freed_vaddr_count];
		spin_release(&slab_global_lock);
		return addr;
	}
	uintptr_t addr = last_slab_addr;
	last_slab_addr += BLOCK_SIZE;
	spin_release(&slab_global_lock);
	return addr;
}

static void push_freed_vaddr(uintptr_t vaddr) {
	spin_acquire(&slab_global_lock);
	if (freed_vaddr_count < MAX_FREED_VADDRS) {
		freed_vaddrs[freed_vaddr_count++] = vaddr;
	}
	spin_release(&slab_global_lock);
}

void vxCreateSlabCache(struct slab_cache** cache, const char* name,
		       const size_t obj_size, size_t alignment,
		       const uintptr_t virt_addr) {
	uintptr_t phys_addr = (uintptr_t) vxPhysBaseAlloc(1);
	LOG_INFO("SLAB", "created new slab cache '%s' at phys 0x%x", name,
		 phys_addr);
	uintptr_t vaddr = virt_addr;

	if (virt_addr == 0) {
		vaddr = get_default_slab_addr();
	}

	// TODO: map to current active page table
	vxMmap(paging_get_highest_page_map(), (uintptr_t) vaddr, phys_addr,
	       0b11);
	*cache = (struct slab_cache*) vaddr;
	memset(*cache, 0, sizeof(struct slab_cache));

	if (virt_addr > 0) {
		(*cache)->default_virt_addr = false;
	} else {
		(*cache)->default_virt_addr = true;
	}

	strcpy((*cache)->name, name);
	(*cache)->phys_addr = phys_addr;
	(*cache)->current_virt_addr = vaddr + BLOCK_SIZE;
	(*cache)->obj_size = obj_size;
	(*cache)->alignment = alignment;

	size_t actual_size = obj_size;
	if (actual_size < sizeof(void*)) {
		actual_size = sizeof(void*);
	}
	if (alignment > 0) {
		actual_size = ALIGN_UP(actual_size, alignment);
	}
	(*cache)->actual_obj_size = actual_size;

	(*cache)->slab_size = BLOCK_SIZE; // 4KB for now
	(*cache)->slabs_full = 0;
	(*cache)->slabs_partial = 0;
	(*cache)->slabs_free = 0;
	(*cache)->total_slabs = 0;
	(*cache)->total_objects = 0;
	(*cache)->free_objects = 0;
	(*cache)->lock = (spinlock_t){0};
}

void* vxSlabAlloc(struct slab_cache* cache) {
	if (cache == NULL) {
		LOG_ERROR("SLAB", "slab cache is NULL");
		return NULL;
	}

	spin_acquire(&cache->lock);

	// // Check if there is a partial slab available
	struct slab* slab = cache->slabs_partial;
	if (slab == NULL) {
		// No partial slab, check for a free slab
		slab = cache->slabs_free;
		if (slab == NULL) {
			// No free slab, create a new one
			uintptr_t phys_addr = (uintptr_t) vxPhysBaseAlloc(1);
			uintptr_t vaddr = cache->current_virt_addr;

			if (cache->default_virt_addr) {
				vaddr = get_default_slab_addr();
			} else {
				cache->current_virt_addr += BLOCK_SIZE;
			}

			vxMmap(paging_get_highest_page_map(), (uintptr_t) vaddr,
			       (uintptr_t) phys_addr, 0b11);

			cache->total_slabs++;
			slab = (struct slab*) vaddr;
			slab->phys_addr = phys_addr;

			memset(slab, 0, sizeof(struct slab));
			slab->magic = 0xDEADBEEF;
			slab->next = cache->slabs_free;
			slab->first_obj = (void*) ((uintptr_t) slab
						   + sizeof(struct slab));

			slab->total_objects =
				(cache->slab_size - sizeof(struct slab))
				/ cache->actual_obj_size;
			slab->free_objects = slab->total_objects;
			slab->free_list = slab->first_obj;

			// Initialize the free list
			uintptr_t obj = (uintptr_t) slab->first_obj;
			for (size_t i = 0; i < slab->total_objects - 1; i++) {
				*(void**) obj =
					(void*) (obj + cache->actual_obj_size);
				obj += cache->actual_obj_size;
			}
			*(void**) obj = NULL; // Last object points to NULL
			cache->slabs_free = slab;
		} // Move slab from free to partial
		cache->slabs_free = slab->next;
		slab->next = cache->slabs_partial;
		cache->slabs_partial = slab;

	} // Allocate object from slab
	void* obj = slab->free_list;
	if (obj == NULL) {
		LOG_ERROR("SLAB",
			  "slab '%s' has no free objects but in partial list!",
			  cache->name);
		spin_release(&cache->lock);
		return NULL; // Should not happen
	}
	slab->free_list = *(void**) obj;
	slab->free_objects--;
	cache->free_objects--;
	cache->total_objects++;
	if (slab->free_objects == 0) {
		// Move slab from partial to full
		cache->slabs_partial = slab->next;
		slab->next = cache->slabs_full;
		cache->slabs_full = slab;
	}

	spin_release(&cache->lock);
	return obj;
}

void slab_cache_destroy(struct slab_cache** cache) {
	if (cache == NULL || *cache == NULL) {
		return;
	}

	spin_acquire(&(*cache)->lock);

	struct slab* slab = (*cache)->slabs_full;
	while (slab) {
		struct slab* next = slab->next;
		if (slab->phys_addr)
			vxPhysBaseFree((void*) slab->phys_addr, 1);
		paging_unmap_page(paging_get_highest_page_map(),
				  (uintptr_t) slab);

		if ((*cache)->default_virt_addr)
			push_freed_vaddr((uintptr_t) slab);

		slab = next;
	}

	slab = (*cache)->slabs_partial;
	while (slab) {
		struct slab* next = slab->next;
		if (slab->phys_addr)
			vxPhysBaseFree((void*) slab->phys_addr, 1);
		paging_unmap_page(paging_get_highest_page_map(),
				  (uintptr_t) slab);

		if ((*cache)->default_virt_addr)
			push_freed_vaddr((uintptr_t) slab);

		slab = next;
	}

	slab = (*cache)->slabs_free;
	while (slab) {
		struct slab* next = slab->next;
		if (slab->phys_addr)
			vxPhysBaseFree((void*) slab->phys_addr, 1);
		paging_unmap_page(paging_get_highest_page_map(),
				  (uintptr_t) slab);

		if ((*cache)->default_virt_addr)
			push_freed_vaddr((uintptr_t) slab);

		slab = next;
	}

	uintptr_t phys_addr = (*cache)->phys_addr;

	spin_release(&(*cache)->lock);

	push_freed_vaddr((uintptr_t) *cache);
	vxPhysBaseFree((void*) phys_addr, 1);
	paging_unmap_page(paging_get_highest_page_map(), (uintptr_t) (*cache));

	// Finally, free the cache itself
	*cache = NULL;
}

void slab_free(struct slab_cache* cache, void* obj) {
	if (cache == NULL || obj == NULL) {
		return;
	}

	spin_acquire(&cache->lock);

	// Find the slab containing the object
	struct slab* slab = cache->slabs_full;
	struct slab* prev = NULL;
	while (slab) {
		if ((uintptr_t) obj >= (uintptr_t) slab->first_obj
		    && (uintptr_t) obj
			       < (uintptr_t) slab->first_obj
					 + slab->total_objects
						   * cache->actual_obj_size) {
			break;
		}
		prev = slab;
		slab = slab->next;
	}

	if (slab == NULL) {
		slab = cache->slabs_partial;
		prev = NULL;
		while (slab) {
			if ((uintptr_t) obj >= (uintptr_t) slab->first_obj
			    && (uintptr_t) obj
				       < (uintptr_t) slab->first_obj
						 + slab->total_objects
							   * cache->actual_obj_size) {
				break;
			}
			prev = slab;
			slab = slab->next;
		}
	}

	if (slab == NULL) {
		spin_release(&cache->lock);
		return;
	}

	// Free the object
	*(void**) obj = slab->free_list;
	slab->free_list = obj;
	slab->free_objects++;
	cache->free_objects++;
	cache->total_objects--;

	if (slab->free_objects == 1) {
		// Move slab from full to partial
		if (prev) {
			prev->next = slab->next;
		} else {
			cache->slabs_full = slab->next;
		}
		slab->next = cache->slabs_partial;
		cache->slabs_partial = slab;
	} else if (slab->free_objects == slab->total_objects) {
		// Move slab from partial to free
		if (prev) {
			prev->next = slab->next;
		} else {
			cache->slabs_partial = slab->next;
		}
		slab->next = cache->slabs_free;
		cache->slabs_free = slab;
	}

	spin_release(&cache->lock);
}
