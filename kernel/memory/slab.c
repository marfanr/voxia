#include "slab.h"
#include <cpu/irq_lock.h>
#include <type.h>

#include <libk/serial.h>
#include <str.h>

#include <hal/cpu/paging.h>
#include <libk/grow_bitmap.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <spinlock.h>
#include <str.h>

typedef struct {
	uint32_t magic;
	struct slab* parent_slab;
} slab_metadata_t;

#define SLAB_MAGIC 0x51AB51AB
#define DEFAULT_SLAB_ADDR 0xFFFF828000000000

static spinlock_t slab_global_lock = {0};
static grow_bitmap_t slab_vaddr_bitmap;
static boolean_t slab_vaddr_initialized = false;

static uintptr_t get_default_slab_addr() {
	uintptr_t flags = irq_save();
	spin_acquire(&slab_global_lock);
	if (!slab_vaddr_initialized) {
		grow_bitmap_init(&slab_vaddr_bitmap, DEFAULT_SLAB_ADDR,
		                 BLOCK_SIZE, 2048);
		slab_vaddr_initialized = true;
	}
	uintptr_t addr = grow_bitmap_alloc(&slab_vaddr_bitmap);
	if (addr == 0) {
		LOG_ERROR("SLAB", "Out of virtual memory in bitmap!");
	}
	spin_release(&slab_global_lock);
	irq_restore(flags);
	return addr;
}

static void push_freed_vaddr(uintptr_t vaddr) {
	spin_acquire(&slab_global_lock);
	grow_bitmap_free(&slab_vaddr_bitmap, vaddr);
	spin_release(&slab_global_lock);
}

void vxCreateSlabCache(struct slab_cache** cache, const char* name,
                       const size_t obj_size, size_t alignment,
                       const uintptr_t virt_addr) {
	uintptr_t phys_addr = (uintptr_t)phys_base_alloc(1);
	LOG_INFO("SLAB", "created new slab cache '%s' at phys 0x%x", name,
	         phys_addr);
	uintptr_t vaddr = virt_addr;

	if (virt_addr == 0) {
		vaddr = get_default_slab_addr();
	}

	// TODO: map to current active page table
	paging_mmap(paging_get_highest_page_map(), (uintptr_t)vaddr, phys_addr,
	            0b11);
	*cache = (struct slab_cache*)vaddr;
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

	size_t actual_size = obj_size + sizeof(slab_metadata_t);
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

	uintptr_t flags = irq_save();
	spin_acquire(&cache->lock);

	// // Check if there is a partial slab available
	struct slab* slab = cache->slabs_partial;
	if (slab == NULL) {
		// No partial slab, check for a free slab
		slab = cache->slabs_free;
		if (slab == NULL) {
			// No free slab, create a new one
			uintptr_t phys_addr = (uintptr_t)phys_base_alloc(1);
			uintptr_t vaddr = cache->current_virt_addr;

			if (cache->default_virt_addr) {
				vaddr = get_default_slab_addr();
			} else {
				cache->current_virt_addr += BLOCK_SIZE;
			}

			paging_mmap(paging_get_highest_page_map(),
			            (uintptr_t)vaddr, (uintptr_t)phys_addr,
			            0b11);

			cache->total_slabs++;
			slab = (struct slab*)vaddr;
			slab->phys_addr = phys_addr;

			memset(slab, 0, sizeof(struct slab));
			slab->magic = 0xDEADBEEF;
			slab->next = cache->slabs_free;
			slab->first_obj =
			    (void*)((uintptr_t)slab + sizeof(struct slab));

			slab->total_objects =
			    (cache->slab_size - sizeof(struct slab)) /
			    cache->actual_obj_size;
			slab->free_objects = slab->total_objects;
			slab->free_list = slab->first_obj;

			// Initialize the free list
			uintptr_t obj = (uintptr_t)slab->first_obj;
			for (size_t i = 0; i < slab->total_objects - 1; i++) {
				*(void**)obj =
				    (void*)(obj + cache->actual_obj_size);
				obj += cache->actual_obj_size;
			}
			*(void**)obj = NULL; // Last object points to NULL

			slab->next = cache->slabs_free;
			slab->prev = NULL;
			if (cache->slabs_free)
				cache->slabs_free->prev = slab;
			cache->slabs_free = slab;
		} // Move slab from free to partial

		if (slab->prev)
			slab->prev->next = slab->next;
		else
			cache->slabs_free = slab->next;
		if (slab->next)
			slab->next->prev = slab->prev;

		slab->next = cache->slabs_partial;
		slab->prev = NULL;
		if (cache->slabs_partial)
			cache->slabs_partial->prev = slab;
		cache->slabs_partial = slab;

	} // Allocate object from slab
	void* obj = slab->free_list;
	if (obj == NULL) {
		LOG_ERROR("SLAB",
		          "slab '%s' has no free objects but in partial list!",
		          cache->name);
		spin_release(&cache->lock);
		irq_restore(flags);
		return NULL; // Should not happen
	}
	slab->free_list = *(void**)obj;
	slab->free_objects--;
	cache->free_objects--;
	cache->total_objects++;
	if (slab->free_objects == 0) {
		// Move slab from partial to full
		if (slab->prev)
			slab->prev->next = slab->next;
		else
			cache->slabs_partial = slab->next;
		if (slab->next)
			slab->next->prev = slab->prev;

		slab->next = cache->slabs_full;
		slab->prev = NULL;
		if (cache->slabs_full)
			cache->slabs_full->prev = slab;
		cache->slabs_full = slab;
	}

	slab_metadata_t* meta = (slab_metadata_t*)obj;
	meta->magic = SLAB_MAGIC;
	meta->parent_slab = slab;

	spin_release(&cache->lock);
	irq_restore(flags);
	return (void*)((uintptr_t)obj + sizeof(slab_metadata_t));
}

void slab_cache_destroy(struct slab_cache** cache) {
	if (cache == NULL || *cache == NULL) {
		return;
	}

	uintptr_t flags = irq_save();
	spin_acquire(&(*cache)->lock);

	struct slab* slab = (*cache)->slabs_full;
	while (slab) {
		struct slab* next = slab->next;
		if (slab->phys_addr)
			vxPhysBaseFree((void*)slab->phys_addr, 1);
		paging_unmap_page(paging_get_highest_page_map(),
		                  (uintptr_t)slab);

		if ((*cache)->default_virt_addr)
			push_freed_vaddr((uintptr_t)slab);

		slab = next;
	}

	slab = (*cache)->slabs_partial;
	while (slab) {
		struct slab* next = slab->next;
		if (slab->phys_addr)
			vxPhysBaseFree((void*)slab->phys_addr, 1);
		paging_unmap_page(paging_get_highest_page_map(),
		                  (uintptr_t)slab);

		if ((*cache)->default_virt_addr)
			push_freed_vaddr((uintptr_t)slab);

		slab = next;
	}

	slab = (*cache)->slabs_free;
	while (slab) {
		struct slab* next = slab->next;
		if (slab->phys_addr)
			vxPhysBaseFree((void*)slab->phys_addr, 1);
		paging_unmap_page(paging_get_highest_page_map(),
		                  (uintptr_t)slab);

		if ((*cache)->default_virt_addr)
			push_freed_vaddr((uintptr_t)slab);

		slab = next;
	}

	uintptr_t phys_addr = (*cache)->phys_addr;

	spin_release(&(*cache)->lock);
	irq_restore(flags);

	push_freed_vaddr((uintptr_t)*cache);
	vxPhysBaseFree((void*)phys_addr, 1);
	paging_unmap_page(paging_get_highest_page_map(), (uintptr_t)(*cache));

	// Finally, free the cache itself
	*cache = NULL;
}

void slab_free(struct slab_cache* cache, void* obj) {
	if (cache == NULL || obj == NULL) {
		return;
	}

	slab_metadata_t* meta =
	    (slab_metadata_t*)((uintptr_t)obj - sizeof(slab_metadata_t));
	if (meta->magic != SLAB_MAGIC) {
		LOG_ERROR("SLAB", "Invalid magic on slab_free!");
		return;
	}

	struct slab* slab = meta->parent_slab;

	uintptr_t flags = irq_save();
	spin_acquire(&cache->lock);

	// Free the object
	void* block_start = (void*)meta;
	*(void**)block_start = slab->free_list;
	slab->free_list = block_start;

	slab->free_objects++;
	cache->free_objects++;
	cache->total_objects--;

	if (slab->free_objects == 1) {
		// Move slab from full to partial
		if (slab->prev)
			slab->prev->next = slab->next;
		else
			cache->slabs_full = slab->next;
		if (slab->next)
			slab->next->prev = slab->prev;

		slab->next = cache->slabs_partial;
		slab->prev = NULL;
		if (cache->slabs_partial)
			cache->slabs_partial->prev = slab;
		cache->slabs_partial = slab;
	} else if (slab->free_objects == slab->total_objects) {
		// Move slab from partial to free
		if (slab->prev)
			slab->prev->next = slab->next;
		else
			cache->slabs_partial = slab->next;
		if (slab->next)
			slab->next->prev = slab->prev;

		slab->next = cache->slabs_free;
		slab->prev = NULL;
		if (cache->slabs_free)
			cache->slabs_free->prev = slab;
		cache->slabs_free = slab;
	}

	spin_release(&cache->lock);
	irq_restore(flags);
}
