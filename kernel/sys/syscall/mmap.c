#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "sys/err_no.h"
#include "vfs/vnode.h"
#include <str.h>
#include <string.h>
#include <sys/fd.h>
#include <sys/syscall.h>

// menyesuaikan dengan musl
#define MAP_FAILED ((void*)-1)

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_SHARED_VALIDATE 0x03
#define MAP_TYPE 0x0f
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define MAP_ANONYMOUS MAP_ANON
#define MAP_NORESERVE 0x4000
#define MAP_GROWSDOWN 0x0100
#define MAP_DENYWRITE 0x0800
#define MAP_EXECUTABLE 0x1000
#define MAP_LOCKED 0x2000
#define MAP_POPULATE 0x8000
#define MAP_NONBLOCK 0x10000
#define MAP_STACK 0x20000
#define MAP_HUGETLB 0x40000
#define MAP_SYNC 0x80000
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FILE 0

#define MAP_HUGE_SHIFT 26
#define MAP_HUGE_MASK 0x3f
#define MAP_HUGE_16KB (14 << 26)
#define MAP_HUGE_64KB (16 << 26)
#define MAP_HUGE_512KB (19 << 26)
#define MAP_HUGE_1MB (20 << 26)
#define MAP_HUGE_2MB (21 << 26)
#define MAP_HUGE_8MB (23 << 26)
#define MAP_HUGE_16MB (24 << 26)
#define MAP_HUGE_32MB (25 << 26)
#define MAP_HUGE_256MB (28 << 26)
#define MAP_HUGE_512MB (29 << 26)
#define MAP_HUGE_1GB (30 << 26)
#define MAP_HUGE_2GB (31 << 26)
#define MAP_HUGE_16GB (34U << 26)

static uint64_t mmap_prot_to_flags(int prot) {
	if (prot == PROT_NONE)
		return 0;

	uint64_t flags = PAGE_USER;
	if (prot & (PROT_READ | PROT_WRITE | PROT_EXEC))
		flags |= PAGE_PRESENT;
	if (prot & PROT_WRITE)
		flags |= PAGE_WRITABLE;
	if (!(prot & PROT_EXEC))
		flags |= PAGE_NO_EXECUTE;

	return flags;
}

static uintptr_t mmap_resolve_virt_addr(void* addr, int flags, struct virtual_memory_page* vm_page, size_t len_4kb) {
	if (flags & MAP_FIXED) {
		if (!addr)
			return 0;
		return (uintptr_t)addr;
	}

	return vma_lookup_free_vaddr(vm_page, USER_MMAP_BASE, len_4kb);
}

/*
 * mmap_handle_anonymous: allocate physical pages and map them into the process.
 *
 * keep_writable: if true, the pages are always left writable (RW) after setup
 * so that the caller can copy file content into them. The caller is responsible
 * for calling paging_multiple_mmap() with the final protection flags afterwards.
 * If false, the final mmap_flags protection is applied immediately (used for
 * pure anonymous mappings where no file data needs to be written after return).
 */
static void* mmap_handle_anonymous(process_t* procc, void* addr, int flags, size_t len_4kb, uint64_t mmap_flags, bool keep_writable) {
	uintptr_t virt_addr = mmap_resolve_virt_addr(addr, flags, procc->vm_page, len_4kb);

	if (!virt_addr) {
		serial2_printf("mmap_handle_anonymous failed: virt_addr=0\n");
		return (flags & MAP_FIXED) ? (void*)-EINVAL : (void*)-ENOMEM;
	}

	uintptr_t phys = (uintptr_t)phys_base_alloc(len_4kb);
	if (!phys) {
		serial2_printf("mmap_handle_anonymous failed: phys_base_alloc returned 0\n");
		return (void*)-ENOMEM;
	}

	/*
	 * Always map writable first so memset (and caller's file copy) can
	 * write to the freshly allocated pages regardless of final prot.
	 */
	uint64_t temp_flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_NO_EXECUTE;
	paging_multiple_mmap(procc->page, virt_addr, phys, len_4kb, temp_flags);
	vma_register(procc->vm_page, phys, virt_addr, len_4kb * BLOCK_SIZE, mmap_flags);

	paging_reload(procc->page);
	memset((void*)virt_addr, 0, len_4kb * BLOCK_SIZE);

	if (!keep_writable) {
		/*
		 * Apply final protection now (e.g. PROT_READ, PROT_READ|EXEC).
		 * For file-backed mappings keep_writable=true so we defer this
		 * until after the file data has been copied in.
		 */
		uint64_t required = PAGE_PRESENT | PAGE_WRITABLE;
		if ((mmap_flags & required) != required) {
			paging_multiple_mmap(procc->page, virt_addr, phys, len_4kb, mmap_flags);
			paging_reload(procc->page);
		}
	}

	// serial2_printf("mmap_handle_anonymous returning 0x%lx\n", virt_addr);
	return (void*)virt_addr;
}

void* syscall_mmap(void* addr, size_t len, int prot, int flags, int fd, long off) {
	(void)fd;
	(void)off;
	auto thr = get_current_core_data()->active_thread;
	auto procc = thr ? thr->process : nullptr;

	if (!thr || !procc)
		return (void*)-ENOENT;

	// serial_trace("mmap_request: addr=0x%x len=0x%x prot=%d "
	//              "flags=0x%x fd=0x%x off=0x%x\n",
	//              addr, len, prot, flags, fd, off);

	if (len == 0)
		return (void*)-EINVAL;

	size_t len_4kb = ALIGN_UP(len, BLOCK_SIZE) / BLOCK_SIZE;
	uint64_t mmap_flags = mmap_prot_to_flags(prot);

	if (flags & MAP_ANONYMOUS)
		return mmap_handle_anonymous(procc, addr, flags, len_4kb, mmap_flags,
		                             /*keep_writable=*/false);

	if (flags & MAP_PRIVATE) {
		/*
		 * File-backed MAP_PRIVATE: allocate pages and keep them writable so
		 * we can copy file content below, then apply final protection after.
		 */
		bool need_file = !(flags & MAP_ANONYMOUS) && fd >= 0;
		void* virt_addr = mmap_handle_anonymous(procc, addr, flags, len_4kb, mmap_flags,
		                                        /*keep_writable=*/need_file);
		if ((uintptr_t)virt_addr > 0xFFFFFFFFFFFFF000)
			return virt_addr;

		if (need_file) {
			auto fdt = (struct fdtable*)procc->fdtable;
			if (fd < (int)fdt->max_fds) {
				auto curr_fd = fdt->fds[fd];
				if (curr_fd && curr_fd->vnode && curr_fd->ops) {
					auto ops = (vops_file_t*)curr_fd->ops;
					if (ops->read) {
						size_t max_chunk = 16384;
						void* kbuf = kalloc(max_chunk);
						if (kbuf) {
							size_t read_total = 0;
							/*
							 * paging_reload was already done inside
							 * mmap_handle_anonymous; pages are writable.
							 */
							while (read_total < len) {
								size_t chunk = len - read_total;
								if (chunk > max_chunk)
									chunk = max_chunk;
								int r = ops->read(curr_fd->vnode, kbuf, chunk, (size_t)off + read_total);
								if (r <= 0)
									break;
								/* Pages are writable; copy data in. */
								memcopy((void*)((uintptr_t)virt_addr + read_total), kbuf, (size_t)r);
								read_total += (size_t)r;
							}
							kfree2(kbuf);
						}
						/*
						 * File copy done. Now apply final protection
						 * (e.g. PROT_READ|EXEC for text segments).
						 * Skip if already writable (mmap_flags already
						 * includes PAGE_WRITABLE).
						 */
						uint64_t required = PAGE_PRESENT | PAGE_WRITABLE;
						if ((mmap_flags & required) != required) {
							/*
							 * Remap each page with the correct phys address
							 * preserved; use vaddr_to_paddr to find the
							 * existing physical frame so we don't clobber it.
							 */
							for (size_t pg = 0; pg < len_4kb; pg++) {
								uintptr_t v = (uintptr_t)virt_addr + pg * BLOCK_SIZE;
								uint64_t p = vaddr_to_paddr(procc->page, v);
								if (p != 0)
									paging_mmap(procc->page, v, p, mmap_flags);
							}
							paging_reload(procc->page);
						}
					}
				}
			}
		}
		return virt_addr;
	}

	serial2_printf("mmap: unsupported flags=0x%x\n", flags);
	return (void*)-ENOSPC;
}