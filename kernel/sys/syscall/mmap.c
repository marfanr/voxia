#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "sys/err_no.h"
#include <str.h>
#include <sys/syscall.h>

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

void* syscall_mmap(void* addr, size_t len, int prot, int flags, int fd,
                   long off) {
	auto thr = get_current_core_data()->active_thread;
	auto procc = thr->process;
	if (!thr || !procc) {
		return (void*)-ENOENT;
	}

	serial_trace("mmap_request: addr 0x%x, len 0x%x, prot %d, flags "
	             "0x%x, fd 0x%x, off 0x%x\n",
	             addr, len, prot, flags, fd, off);

	if (len == 0) {
		return (void*)-EINVAL;
	}

	uint64_t mmap_flags = 0;
	if (prot != PROT_NONE) {
		if (prot & PROT_READ)
			mmap_flags |= PAGE_PRESENT;
		if (prot & PROT_WRITE)
			mmap_flags |= PAGE_WRITABLE;
		if (!(prot & PROT_EXEC))
			mmap_flags |= PAGE_NO_EXECUTE;

		if (prot & (PROT_READ | PROT_WRITE | PROT_EXEC))
			mmap_flags |= PAGE_PRESENT;
	}

	auto len_4kb = ALIGN_UP(len, 0x1000) / 0x1000;

	if (flags & MAP_ANONYMOUS) {
		serial2_printf("anonymous flag \n");

		uintptr_t virt_addr = 0;

		if (flags & MAP_FIXED) {
			serial2_printf("fixed flag\n");
			if (!addr) {
				return (void*)-EINVAL; // MAP_FIXED with NULL is
				                       // invalid
			}
			virt_addr = (uintptr_t)addr;
		} else {
			// find a free region in the process virtual address
			// space
			virt_addr = vma_lookup_free_vaddr(
			    procc->vm_page, VMA_REGION_PROCESS, len_4kb);

			if (!virt_addr) {
				return (void*)-ENOMEM;
			}
		}

		auto phys = phys_base_alloc(len_4kb);
		if (!addr || !phys) {
			return (void*)-EINVAL;
		}


		vxMultipleMmap(thr->page, (uint64_t)virt_addr, (uint64_t)phys,
		               len_4kb, mmap_flags);
		serial2_printf("mmaped 0x%x (%b)\n", virt_addr, mmap_flags);
		
		vma_register(procc->vm_page, (uintptr_t)phys, (uintptr_t)addr,
		             len_4kb * BLOCK_SIZE);

		return (void*)virt_addr;
	}

	if (flags & MAP_PRIVATE) {
		serial2_printf("private flag\n");
	}

	serial2_printf("mmap: not supported\n");
	return (void*)-ENOSPC;
}