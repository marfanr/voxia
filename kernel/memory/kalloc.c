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
#define KALLOC_REDZONE_SIZE 16
#define KALLOC_REDZONE_MAGIC 0xFDEAABEEU

typedef struct {
	uintptr_t addr;
	size_t size; /* BLOCK_SIZE (page) */
} freed_t;

typedef struct {
	size_t size;	/* original requested size */
	uint32_t magic; /* magic number for validation */
	uint32_t _pad;	/* explicit padding untuk ensure 16 bytes */
} kalloc_metadata_t;

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

static void setup_redzone(void* ptr, size_t size) {
	uint32_t* p = (uint32_t*) ptr;
	for (size_t i = 0; i < size / sizeof(uint32_t); i++)
		p[i] = KALLOC_REDZONE_MAGIC;
}

static int check_redzone(void* ptr, size_t size) {
	uint32_t* p = (uint32_t*) ptr;
	for (size_t i = 0; i < size / sizeof(uint32_t); i++) {
		if (p[i] != KALLOC_REDZONE_MAGIC)
			return 0;
	}
	return 1;
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
		void* slot = cc->c_##BUCKET;                                   \
		cc->c_##BUCKET = *(void**) slot;                               \
		cc->c_##BUCKET##_count--;                                      \
		unlock_irqrestore(&cc->lock, flags);                           \
                                                                               \
		kalloc_metadata_t* meta = (kalloc_metadata_t*) slot;           \
		meta->size = size;                                             \
		meta->magic = KALLOC_REDZONE_MAGIC;                            \
                                                                               \
		void* red_before = (void*) ((uintptr_t) slot                   \
					    + sizeof(kalloc_metadata_t));      \
		setup_redzone(red_before, KALLOC_REDZONE_SIZE);                \
                                                                               \
		void* data = (void*) ((uintptr_t) red_before                   \
				      + KALLOC_REDZONE_SIZE);                  \
		void* red_after = (void*) ((uintptr_t) data + size);           \
		setup_redzone(red_after, KALLOC_REDZONE_SIZE);                 \
                                                                               \
		return data;                                                   \
	} while (0)

KERNEL_API void* kalloc(size_t size) {
	if (size == 0)
		return NULL;

	/* Check if we can fit in slab buckets with metadata + red zones */
	size_t metadata_overhead =
		sizeof(kalloc_metadata_t) + 2 * KALLOC_REDZONE_SIZE;

	if (size + metadata_overhead <= 64) {
		KALLOC_SLAB_ALLOC(64);
	} else if (size + metadata_overhead <= 128) {
		KALLOC_SLAB_ALLOC(128);
	} else if (size + metadata_overhead <= 256) {
		KALLOC_SLAB_ALLOC(256);
	} else if (size + metadata_overhead <= 512) {
		KALLOC_SLAB_ALLOC(512);
	} else if (size + metadata_overhead <= 1024) {
		KALLOC_SLAB_ALLOC(1024);
	} else if (size + metadata_overhead <= 2048) {
		KALLOC_SLAB_ALLOC(2048);
	}

	/* Large alloc (> 2048 byte), or doesn't fit in slab */
	size_t total_size = size + metadata_overhead;
	size_t page_count = ALIGN_UP(total_size, BLOCK_SIZE) / BLOCK_SIZE;

	uintptr_t gflags = lock_irqsave(&kalloc_global_lock);

	uintptr_t phys = (uintptr_t) vxPhysBaseAlloc(page_count);
	uintptr_t virt = vaddr_alloc_locked(page_count);

	vxMultipleMmap(paging_get_highest_page_map(), virt, phys, page_count,
		       PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
	vma_register(phys, virt, page_count * BLOCK_SIZE);

	unlock_irqrestore(&kalloc_global_lock, gflags);

	/* Setup layout: [metadata] [red_before] [data] [red_after] */
	kalloc_metadata_t* meta = (kalloc_metadata_t*) virt;
	meta->size = size;
	meta->magic = KALLOC_REDZONE_MAGIC;

	void* red_before =
		(void*) ((uintptr_t) virt + sizeof(kalloc_metadata_t));
	setup_redzone(red_before, KALLOC_REDZONE_SIZE);

	void* data = (void*) ((uintptr_t) red_before + KALLOC_REDZONE_SIZE);
	void* red_after = (void*) ((uintptr_t) data + size);
	setup_redzone(red_after, KALLOC_REDZONE_SIZE);

	return data;
}

KERNEL_API void kfree(void* ptr, size_t size) {
	if (ptr == NULL || size == 0)
		return;

	uint32_t cpu = vxGetCoreData()->core_id;
	struct kalloc_cpu_cache* cc = &cpu_caches[cpu];
	uintptr_t cpu_flags = lock_irqsave(&cc->lock);

	/* Check metadata at offset from pointer */
	kalloc_metadata_t* meta =
		(kalloc_metadata_t*) ((uintptr_t) ptr
				      - sizeof(kalloc_metadata_t)
				      - KALLOC_REDZONE_SIZE);
	if (meta->magic != KALLOC_REDZONE_MAGIC) {
		unlock_irqrestore(&cc->lock, cpu_flags);
		LOG2_WARN("KALLOC", "metadata failed");
		return;
	}

	/* Verify red zones */
	void* red_before =
		(void*) ((uintptr_t) meta + sizeof(kalloc_metadata_t));
	if (!check_redzone(red_before, KALLOC_REDZONE_SIZE)) {
		unlock_irqrestore(&cc->lock, cpu_flags);
		LOG2_WARN("KALLOC", "red zone overllaping");
		return;
	}

	void* red_after = (void*) ((uintptr_t) ptr + meta->size);
	if (!check_redzone(red_after, KALLOC_REDZONE_SIZE)) {
		unlock_irqrestore(&cc->lock, cpu_flags);
		LOG2_WARN("KALLOC", "red zone overllaping");
		return;
	}

	size_t metadata_overhead =
		sizeof(kalloc_metadata_t) + 2 * KALLOC_REDZONE_SIZE;

	if (size + metadata_overhead <= 64) {
		void* slot = (void*) meta;
		memset(slot, 0, 64);
		*(void**) slot = cc->c_64;
		cc->c_64 = slot;
		cc->c_64_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 128) {
		void* slot = (void*) meta;
		memset(slot, 0, 128);
		*(void**) slot = cc->c_128;
		cc->c_128 = slot;
		cc->c_128_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 256) {
		void* slot = (void*) meta;
		memset(slot, 0, 256);
		*(void**) slot = cc->c_256;
		cc->c_256 = slot;
		cc->c_256_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 512) {
		void* slot = (void*) meta;
		memset(slot, 0, 512);
		*(void**) slot = cc->c_512;
		cc->c_512 = slot;
		cc->c_512_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 1024) {
		void* slot = (void*) meta;
		memset(slot, 0, 1024);
		*(void**) slot = cc->c_1024;
		cc->c_1024 = slot;
		cc->c_1024_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 2048) {
		void* slot = (void*) meta;
		memset(slot, 0, 2048);
		*(void**) slot = cc->c_2048;
		cc->c_2048 = slot;
		cc->c_2048_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else {
		unlock_irqrestore(&cc->lock, cpu_flags);

		uintptr_t gflags = lock_irqsave(&kalloc_global_lock);

		size_t total_size = size + metadata_overhead;
		memset(meta, 0, total_size);

		virtual_memory_t* v = vma_find((uintptr_t) meta);
		if (!v) {
			unlock_irqrestore(&kalloc_global_lock, gflags);
			return;
		}

		size_t page_count =
			ALIGN_UP(total_size, BLOCK_SIZE) / BLOCK_SIZE;

		vxPhysBaseFree((void*) v->phys_address, page_count);
		paging_unmap_fill(paging_get_highest_page_map(),
				  (uintptr_t) meta, page_count);
		vma_unregister((uintptr_t) meta);

		if (freed_vaddr_count < MAX_FREED_VADDRS) {
			freed_vaddrs[freed_vaddr_count++] = (freed_t){
				.addr = (uintptr_t) meta,
				.size = page_count,
			};
		}

		unlock_irqrestore(&kalloc_global_lock, gflags);
	}
}

KERNEL_API void kfree2(void* ptr) {
	if (ptr == NULL)
		return;

	uint32_t cpu = vxGetCoreData()->core_id;
	struct kalloc_cpu_cache* cc = &cpu_caches[cpu];
	uintptr_t cpu_flags = lock_irqsave(&cc->lock);

	/* Check metadata at offset from pointer */
	kalloc_metadata_t* meta =
		(kalloc_metadata_t*) ((uintptr_t) ptr
				      - sizeof(kalloc_metadata_t)
				      - KALLOC_REDZONE_SIZE);
	if (meta->magic != KALLOC_REDZONE_MAGIC) {
		unlock_irqrestore(&cc->lock, cpu_flags);
		return;
	}

	auto size = meta->size;

	/* Verify red zones */
	void* red_before =
		(void*) ((uintptr_t) meta + sizeof(kalloc_metadata_t));
	if (!check_redzone(red_before, KALLOC_REDZONE_SIZE)) {
		unlock_irqrestore(&cc->lock, cpu_flags);
		LOG2_WARN("KALLOC", "red zone overllaping");
		return;
	}

	void* red_after = (void*) ((uintptr_t) ptr + meta->size);
	if (!check_redzone(red_after, KALLOC_REDZONE_SIZE)) {
		unlock_irqrestore(&cc->lock, cpu_flags);
		LOG2_WARN("KALLOC", "red zone overllaping");
		return;
	}

	size_t metadata_overhead =
		sizeof(kalloc_metadata_t) + 2 * KALLOC_REDZONE_SIZE;

	if (size + metadata_overhead <= 64) {
		void* slot = (void*) meta;
		memset(slot, 0, 64);
		*(void**) slot = cc->c_64;
		cc->c_64 = slot;
		cc->c_64_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 128) {
		void* slot = (void*) meta;
		memset(slot, 0, 128);
		*(void**) slot = cc->c_128;
		cc->c_128 = slot;
		cc->c_128_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 256) {
		void* slot = (void*) meta;
		memset(slot, 0, 256);
		*(void**) slot = cc->c_256;
		cc->c_256 = slot;
		cc->c_256_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 512) {
		void* slot = (void*) meta;
		memset(slot, 0, 512);
		*(void**) slot = cc->c_512;
		cc->c_512 = slot;
		cc->c_512_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 1024) {
		void* slot = (void*) meta;
		memset(slot, 0, 1024);
		*(void**) slot = cc->c_1024;
		cc->c_1024 = slot;
		cc->c_1024_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else if (size + metadata_overhead <= 2048) {
		void* slot = (void*) meta;
		memset(slot, 0, 2048);
		*(void**) slot = cc->c_2048;
		cc->c_2048 = slot;
		cc->c_2048_count++;
		unlock_irqrestore(&cc->lock, cpu_flags);
	} else {
		unlock_irqrestore(&cc->lock, cpu_flags);

		uintptr_t gflags = lock_irqsave(&kalloc_global_lock);

		size_t total_size = size + metadata_overhead;
		memset(meta, 0, total_size);

		virtual_memory_t* v = vma_find((uintptr_t) meta);
		if (!v) {
			unlock_irqrestore(&kalloc_global_lock, gflags);
			return;
		}

		size_t page_count =
			ALIGN_UP(total_size, BLOCK_SIZE) / BLOCK_SIZE;
		// serial2_printf("page count %d\n", page_count);

		vxPhysBaseFree((void*) v->phys_address, page_count);
		paging_unmap_fill(paging_get_highest_page_map(),
				  (uintptr_t) meta, page_count);
		vma_unregister((uintptr_t) meta);

		if (freed_vaddr_count < MAX_FREED_VADDRS) {
			freed_vaddrs[freed_vaddr_count++] = (freed_t){
				.addr = (uintptr_t) meta,
				.size = page_count,
			};
		}

		unlock_irqrestore(&kalloc_global_lock, gflags);
	}
}