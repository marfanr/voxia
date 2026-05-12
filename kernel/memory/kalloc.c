#include "libk/serial.h"
#include "libk/type.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include <hal/cpu/paging.h>
#include <hal/cpu/spinlock.h>
#include <str.h>
#include <memory/kalloc.h>

#define KALLOC_BASE_ADDR 0xFFFFFE0000000000
static uintptr_t kalloc_next_addr = KALLOC_BASE_ADDR;

static spinlock_t kalloc_lock = {0};

struct kalloc_cache {
	size_t c_64_count;
	size_t c_128_count;
	size_t c_256_count;
	size_t c_512_count;
	size_t c_1024_count;
	size_t c_2048_count;

	void* c_64;
	void* c_128;
	void* c_256;
	void* c_512;
	void* c_1024;
	void* c_2048;
};

static struct kalloc_cache cache;

#define MAX_FREED_VADDRS 512

typedef struct {
	uintptr_t addr;
	size_t size;
} freed_t;

static freed_t freed_vaddrs[MAX_FREED_VADDRS] = {0};
static size_t freed_vaddr_count = 0;

/* get_vaddr hanya dipakai oleh slab allocator (__alloc_4k).
 * Untuk large alloc (> 2048), vaddr dikelola langsung di kalloc()
 * supaya logika freed slot dan bump allocator tidak tercampur. */
static uintptr_t get_vaddr(size_t count) {
	for (size_t i = 0; i < freed_vaddr_count; i++) {
		if (freed_vaddrs[i].size >= count) {
			uintptr_t current_vaddr = freed_vaddrs[i].addr;

			freed_vaddrs[i].addr += count * BLOCK_SIZE;
			freed_vaddrs[i].size -= count;

			if (freed_vaddrs[i].size == 0) {
				freed_vaddrs[i] =
					freed_vaddrs[--freed_vaddr_count];
			}
			return current_vaddr;
		}
	}
	/* Tidak ada freed slot yang cocok — pakai bump allocator */
	uintptr_t addr = kalloc_next_addr;
	kalloc_next_addr += count * BLOCK_SIZE;
	return addr;
}

static void* __alloc_4k(void) {
	uintptr_t phys_addr = (uintptr_t) vxPhysBaseAlloc(1);
	uintptr_t virt_addr = get_vaddr(1);
	vxMmap(paging_get_highest_page_map(), virt_addr, phys_addr,
	       PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
	vma_register((uintptr_t) phys_addr, (uintptr_t) virt_addr, BLOCK_SIZE);
	return (void*) virt_addr;
}

KERNEL_API void* kalloc(size_t size) {
	if (size == 0)
		return NULL;

	spin_acquire(&kalloc_lock);

	if (size <= 64) {
		if (cache.c_64_count == 0) {
			void* new_page = __alloc_4k();
			for (int i = 0; i < BLOCK_SIZE / 64; i++) {
				*(void**) ((uintptr_t) new_page + i * 64) =
					cache.c_64;
				cache.c_64 =
					(void*) ((uintptr_t) new_page + i * 64);
				cache.c_64_count++;
			}
		}
		void* ret = cache.c_64;
		cache.c_64 = *(void**) cache.c_64;
		cache.c_64_count--;
		spin_release(&kalloc_lock);
		return ret;

	} else if (size <= 128) {
		if (cache.c_128_count == 0) {
			void* new_page = __alloc_4k();
			for (int i = 0; i < BLOCK_SIZE / 128; i++) {
				*(void**) ((uintptr_t) new_page + i * 128) =
					cache.c_128;
				cache.c_128 = (void*) ((uintptr_t) new_page
						       + i * 128);
				cache.c_128_count++;
			}
		}
		void* ret = cache.c_128;
		cache.c_128 = *(void**) cache.c_128;
		cache.c_128_count--;
		spin_release(&kalloc_lock);
		return ret;

	} else if (size <= 256) {
		if (cache.c_256_count == 0) {
			void* new_page = __alloc_4k();
			for (int i = 0; i < BLOCK_SIZE / 256; i++) {
				*(void**) ((uintptr_t) new_page + i * 256) =
					cache.c_256;
				cache.c_256 = (void*) ((uintptr_t) new_page
						       + i * 256);
				cache.c_256_count++;
			}
		}
		void* ret = cache.c_256;
		cache.c_256 = *(void**) cache.c_256;
		cache.c_256_count--;
		spin_release(&kalloc_lock);
		return ret;

	} else if (size <= 512) {
		if (cache.c_512_count == 0) {
			void* new_page = __alloc_4k();
			for (int i = 0; i < BLOCK_SIZE / 512; i++) {
				*(void**) ((uintptr_t) new_page + i * 512) =
					cache.c_512;
				cache.c_512 = (void*) ((uintptr_t) new_page
						       + i * 512);
				cache.c_512_count++;
			}
		}
		void* ret = cache.c_512;
		cache.c_512 = *(void**) cache.c_512;
		cache.c_512_count--;
		spin_release(&kalloc_lock);
		return ret;

	} else if (size <= 1024) {
		if (cache.c_1024_count == 0) {
			void* new_page = __alloc_4k();
			for (int i = 0; i < BLOCK_SIZE / 1024; i++) {
				*(void**) ((uintptr_t) new_page + i * 1024) =
					cache.c_1024;
				cache.c_1024 = (void*) ((uintptr_t) new_page
							+ i * 1024);
				cache.c_1024_count++;
			}
		}
		void* ret = cache.c_1024;
		cache.c_1024 = *(void**) cache.c_1024;
		cache.c_1024_count--;
		spin_release(&kalloc_lock);
		return ret;

	} else if (size <= 2048) {
		if (cache.c_2048_count == 0) {
			void* new_page = __alloc_4k();
			for (int i = 0; i < BLOCK_SIZE / 2048; i++) {
				*(void**) ((uintptr_t) new_page + i * 2048) =
					cache.c_2048;
				cache.c_2048 = (void*) ((uintptr_t) new_page
							+ i * 2048);
				cache.c_2048_count++;
			}
		}
		void* ret = cache.c_2048;
		cache.c_2048 = *(void**) cache.c_2048;
		cache.c_2048_count--;
		spin_release(&kalloc_lock);
		return ret;
	}

	/* Large alloc: > 2048 bytes, granularity BLOCK_SIZE (4KB) */
	size_t allocate_size = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;
	uintptr_t phys_addr = (uintptr_t) vxPhysBaseAlloc(allocate_size);
	uintptr_t vaddr = 0;

	bool found_free = false;
	for (size_t i = 0; i < freed_vaddr_count; i++) {
		if (freed_vaddrs[i].size >= allocate_size) {
			/* Simpan addr DULU sebelum entry dimodifikasi */
			vaddr = freed_vaddrs[i].addr;

			freed_vaddrs[i].addr += allocate_size * BLOCK_SIZE;
			freed_vaddrs[i].size -= allocate_size;

			if (freed_vaddrs[i].size == 0) {
				freed_vaddrs[i] =
					freed_vaddrs[--freed_vaddr_count];
			}

			found_free = true;
			break;
		}
	}

	/* Fallback ke bump allocator jika tidak ada freed slot
	 * yang cocok — termasuk kasus freed_vaddr_count > 0 tapi semua
	 * slot terlalu kecil. Sebelumnya bump allocator hanya jalan di
	 * else (freed_vaddr_count == 0), sehingga vaddr bisa tetap 0. */
	if (!found_free) {
		vaddr = kalloc_next_addr;
		kalloc_next_addr += BLOCK_SIZE * allocate_size;
	}

	vxMultipleMmap(paging_get_highest_page_map(), vaddr, phys_addr,
		       allocate_size, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
	vma_register((uintptr_t) phys_addr, (uintptr_t) vaddr, allocate_size);
	
	spin_release(&kalloc_lock);
	return (void*) vaddr;
}

KERNEL_API void kfree(void* ptr, size_t size) {
	if (ptr == NULL || size == 0)
		return;

	spin_acquire(&kalloc_lock);

	if (size <= 64) {
		if ((uintptr_t)ptr % 64 != 0) goto out;
		memset(ptr, 0, size);
		*(void**) ptr = cache.c_64;
		cache.c_64 = ptr;
		cache.c_64_count++;
	} else if (size <= 128) {
		if ((uintptr_t)ptr % 128 != 0) goto out;
		memset(ptr, 0, size);
		*(void**) ptr = cache.c_128;
		cache.c_128 = ptr;
		cache.c_128_count++;
	} else if (size <= 256) {
		if ((uintptr_t)ptr % 256 != 0) goto out;
		memset(ptr, 0, size);
		*(void**) ptr = cache.c_256;
		cache.c_256 = ptr;
		cache.c_256_count++;
	} else if (size <= 512) {
		if ((uintptr_t)ptr % 512 != 0) goto out;
		memset(ptr, 0, size);
		*(void**) ptr = cache.c_512;
		cache.c_512 = ptr;
		cache.c_512_count++;
	} else if (size <= 1024) {
		if ((uintptr_t)ptr % 1024 != 0) goto out;
		memset(ptr, 0, size);
		*(void**) ptr = cache.c_1024;
		cache.c_1024 = ptr;
		cache.c_1024_count++;
	} else if (size <= 2048) {
		if ((uintptr_t)ptr % 2048 != 0) goto out;
		memset(ptr, 0, size);
		*(void**) ptr = cache.c_2048;
		cache.c_2048 = ptr;
		cache.c_2048_count++;
	} else {
		memset(ptr, 0, size);
		virtual_memory_t* v = vma_find((uintptr_t) ptr);
		if (!v) {
			goto out;
		}

		size_t allocate_size = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;
		vxPhysBaseFree((void*) v->phys_address, allocate_size);
		paging_unmap_fill(paging_get_highest_page_map(),
				  (uintptr_t) ptr, allocate_size);
		vma_unregister((uintptr_t) ptr);

		if (freed_vaddr_count < MAX_FREED_VADDRS) {
			freed_vaddrs[freed_vaddr_count++] = (freed_t){
				.addr = (uintptr_t) ptr,
				.size = allocate_size,
			};
		}
	}

out:
	spin_release(&kalloc_lock);
}
