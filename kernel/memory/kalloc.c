#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include <hal/cpu/paging.h>
#include <hal/cpu/spinlock.h>
#include <str.h>
#include <memory/kalloc.h>
#include <hal/cpu/irq_lock.h>
#include <autoconf.h>

#define KALLOC_BASE_ADDR 0xFFFFFE0000000000ULL
#define MAX_FREED_VADDRS 512

typedef struct {
	uintptr_t addr;
	size_t size; /* BLOCK_SIZE (page) */
} freed_t;

struct kalloc_cpu_cache {
	/* free-list heads */
	void* c_64;
	void* c_128;
	void* c_256;
	void* c_512;
	void* c_1024;
	void* c_2048;

	/* jumlah slot tersedia di masing-masing bucket */
	size_t c_64_count;
	size_t c_128_count;
	size_t c_256_count;
	size_t c_512_count;
	size_t c_1024_count;
	size_t c_2048_count;

	spinlock_t lock;

	/* padding agar tiap entry tidak berbagi cache line */
	uint8_t _pad[64
		     - (sizeof(void*) * 6 + sizeof(size_t) * 6
			+ sizeof(spinlock_t))
			       % 64];
} __attribute__((aligned(64)));

static struct kalloc_cpu_cache cpu_caches[VOXIA_MAX_CORE];

static spinlock_t kalloc_global_lock = {0};

static uintptr_t kalloc_next_addr = KALLOC_BASE_ADDR;

static freed_t freed_vaddrs[MAX_FREED_VADDRS];
static size_t freed_vaddr_count = 0;

static inline uintptr_t lock_irqsave(spinlock_t* lk) {
	uintptr_t flags = irq_save(); /* disable IRQ, simpan flags */
	spin_acquire(lk);
	return flags;
}

static inline void unlock_irqrestore(spinlock_t* lk, uintptr_t flags) {
	spin_release(lk);
	irq_restore(flags);
}

static uintptr_t vaddr_alloc_locked(size_t page_count) {
	for (size_t i = 0; i < freed_vaddr_count; i++) {
		if (freed_vaddrs[i].size >= page_count) {
			uintptr_t va = freed_vaddrs[i].addr;
			freed_vaddrs[i].addr += page_count * BLOCK_SIZE;
			freed_vaddrs[i].size -= page_count;
			if (freed_vaddrs[i].size == 0)
				freed_vaddrs[i] =
					freed_vaddrs[--freed_vaddr_count];
			return va;
		}
	}
	/* Bump allocator */
	uintptr_t va = kalloc_next_addr;
	kalloc_next_addr += page_count * BLOCK_SIZE;
	return va;
}

static void* alloc_page_locked(void) {
	uintptr_t phys = (uintptr_t) vxPhysBaseAlloc(1);
	uintptr_t virt = vaddr_alloc_locked(1);
	vxMmap(paging_get_highest_page_map(), virt, phys,
	       PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
	vma_register(phys, virt, BLOCK_SIZE);
	return (void*) virt;
}

#define KALLOC_REFILL(BUCKET)                                                      \
	static void refill_##BUCKET(struct kalloc_cpu_cache* cc,                   \
				    uintptr_t* gflags) {                           \
                                                                                   \
		spin_acquire(&kalloc_global_lock);                                 \
		void* page = alloc_page_locked();                                  \
		spin_release(&kalloc_global_lock);                                 \
                                                                                   \
		for (int i = 0; i < (int) (BLOCK_SIZE / BUCKET); i++) {            \
			void* slot = (void*) ((uintptr_t) page                     \
					      + (size_t) i * BUCKET);              \
			*(void**) slot = cc->c_##BUCKET;                           \
			cc->c_##BUCKET = slot;                                     \
			cc->c_##BUCKET##_count++;                                  \
		}                                                                  \
		(void) gflags; /* tidak dipakai di sini, tapi perlu untuk macro */ \
	}

KALLOC_REFILL(64)
KALLOC_REFILL(128)
KALLOC_REFILL(256)
KALLOC_REFILL(512)
KALLOC_REFILL(1024)
KALLOC_REFILL(2048)

#define KALLOC_SLAB_ALLOC(BUCKET)                                              \
	do {                                                                   \
		uint32_t cpu = vxGetCoreData()->core_id;                       \
		struct kalloc_cpu_cache* cc = &cpu_caches[cpu];                \
		uintptr_t flags = lock_irqsave(&cc->lock);                     \
		if (cc->c_##BUCKET##_count == 0)                               \
			refill_##BUCKET(cc, &flags);                           \
		void* ret = cc->c_##BUCKET;                                    \
		cc->c_##BUCKET = *(void**) cc->c_##BUCKET;                     \
		cc->c_##BUCKET##_count--;                                      \
		unlock_irqrestore(&cc->lock, flags);                           \
		return ret;                                                    \
	} while (0)

#define KALLOC_SLAB_FREE(BUCKET)                                               \
	do {                                                                   \
		if ((uintptr_t) ptr % (BUCKET) != 0)                           \
			goto out_unlock_cpu;                                   \
		uint32_t cpu = vxGetCoreData()->core_id;                       \
		struct kalloc_cpu_cache* cc = &cpu_caches[cpu];                \
		memset(ptr, 0, size);                                          \
		*(void**) ptr = cc->c_##BUCKET;                                \
		cc->c_##BUCKET = ptr;                                          \
		cc->c_##BUCKET##_count++;                                      \
		unlock_irqrestore(&cc->lock, cpu_flags);                       \
		return;                                                        \
	} while (0)

KERNEL_API void* kalloc(size_t size) {
	if (size == 0)
		return NULL;

	else if (size <= 64) {
		KALLOC_SLAB_ALLOC(64);
	} else if (size <= 128) {
		KALLOC_SLAB_ALLOC(128);
	} else if (size <= 256) {
		KALLOC_SLAB_ALLOC(256);
	} else if (size <= 512) {
		KALLOC_SLAB_ALLOC(512);
	} else if (size <= 1024) {
		KALLOC_SLAB_ALLOC(1024);
	} else if (size <= 2048) {
		KALLOC_SLAB_ALLOC(2048);
	}

	/*Large alloc (> 2048 byte)*/
	size_t page_count = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;

	uintptr_t gflags = lock_irqsave(&kalloc_global_lock);

	uintptr_t phys = (uintptr_t) vxPhysBaseAlloc(page_count);
	uintptr_t virt = vaddr_alloc_locked(page_count);

	vxMultipleMmap(paging_get_highest_page_map(), virt, phys, page_count,
		       PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
	vma_register(phys, virt, page_count * BLOCK_SIZE);

	unlock_irqrestore(&kalloc_global_lock, gflags);

	return (void*) virt;
}

KERNEL_API void kfree(void* ptr, size_t size) {
	if (ptr == NULL || size == 0)
		return;

	uint32_t cpu = vxGetCoreData()->core_id;
	struct kalloc_cpu_cache* cc = &cpu_caches[cpu];
	uintptr_t cpu_flags = lock_irqsave(&cc->lock);

	if (size <= 64) {
		KALLOC_SLAB_FREE(64);
	} else if (size <= 128) {
		KALLOC_SLAB_FREE(128);
	} else if (size <= 256) {
		KALLOC_SLAB_FREE(256);
	} else if (size <= 512) {
		KALLOC_SLAB_FREE(512);
	} else if (size <= 1024) {
		KALLOC_SLAB_FREE(1024);
	} else if (size <= 2048) {
		KALLOC_SLAB_FREE(2048);
	} else {

		uintptr_t gflags = lock_irqsave(&kalloc_global_lock);

		memset(ptr, 0, size);

		virtual_memory_t* v = vma_find((uintptr_t) ptr);
		if (!v) {
			unlock_irqrestore(&kalloc_global_lock, gflags);
			goto out_unlock_cpu;
		}

		size_t page_count = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;

		vxPhysBaseFree((void*) v->phys_address, page_count);
		paging_unmap_fill(paging_get_highest_page_map(),
				  (uintptr_t) ptr, page_count);
		vma_unregister((uintptr_t) ptr);

		if (freed_vaddr_count < MAX_FREED_VADDRS) {
			freed_vaddrs[freed_vaddr_count++] = (freed_t){
				.addr = (uintptr_t) ptr,
				.size = page_count,
			};
		}

		unlock_irqrestore(&kalloc_global_lock, gflags);
	}

out_unlock_cpu:
	unlock_irqrestore(&cc->lock, cpu_flags);
}