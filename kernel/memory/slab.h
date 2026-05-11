#ifndef __MEMORY_SLAB_H__
#define __MEMORY_SLAB_H__

#include <type.h>

// Forward declarations
struct slab {
	uint32_t magic;	      // For validation
	struct slab* next;    // Next slab in the list
	void* first_obj;      // Pointer to first object in slab
	void* free_list;      // List of free objects
	size_t total_objects; // Total objects in this slab
	size_t free_objects;  // Number of free objects
	uintptr_t phys_addr;  // Physical address of the page for this slab
};

// Cache managing objects of the same size
struct slab_cache {
	char name[32];	  // Name of the cache
	size_t obj_size;  // Size of each object
	size_t alignment; // Alignment requirement
	size_t slab_size; // Size of each slab

	struct slab* slabs_full;    // Slabs with no free objects
	struct slab* slabs_partial; // Slabs with some free objects
	struct slab* slabs_free;    // Slabs with all objects free

	size_t total_slabs;   // Total number of slabs
	size_t total_objects; // Total objects across all slabs
	size_t free_objects;  // Total free objects

	uintptr_t
		phys_addr; // Physical address of the cache (needed for destroying)
	uintptr_t current_virt_addr; // Virtual address of the cache
	boolean_t default_virt_addr;
};

// Create a new slab cache
void vxCreateSlabCache(struct slab_cache** cache, const char* name,
		       const size_t obj_size, size_t alignment,
		       const uintptr_t virt_addr);

// Destroy a slab cache
void slab_cache_destroy(struct slab_cache** cache);

// Allocate an object from the cache
void* vxSlabAlloc(struct slab_cache* cache);

// Free an object back to the cache
void slab_free(struct slab_cache* cache, void* obj);

// Get cache statistics
void slab_cache_stats(struct slab_cache* cache, size_t* total_objs,
		      size_t* used_objs, size_t* free_objs);

#endif // __MEMORY_SLAB_H__