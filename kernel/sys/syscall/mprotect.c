#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "memory/vm_manager.h"
#include "sys/err_no.h"
#include <sys/syscall.h>

int syscall_mprotect(void* addr, size_t len, int prot) {
	(void)addr;
	(void)len;
	(void)prot;

	serial2_printf("mprotect: addr 0x%x, len 0x%x, prot %d\n", addr, len,
	               prot);
	auto thr = get_current_core_data()->active_thread;
	auto proc = thr->process;

	if (!thr || !proc) {
		return -1;
	}

	uint64_t mmap_flags = PAGE_USER;
	if (!prot) {
		mmap_flags = 0;
	} else {
		if (prot & PROT_READ)
			mmap_flags |= PAGE_PRESENT;
		if (prot & PROT_WRITE)
			mmap_flags |= PAGE_WRITABLE;
		if (!(prot & PROT_EXEC))
			mmap_flags |= PAGE_NO_EXECUTE;

		if (prot & (PROT_READ | PROT_WRITE | PROT_EXEC))
			mmap_flags |= PAGE_PRESENT;
	}

	auto addr_4kb = ALIGN_DOWN((uintptr_t)addr, 0x1000);
	auto len_4kb = ALIGN_UP(len, 0x1000) / 0x1000;

	for (size_t i = 0; i < len_4kb; i++) {
		uintptr_t curr_vaddr = addr_4kb + (i * 0x1000);
		uint64_t phys = vaddr_to_paddr(proc->page, curr_vaddr);
		
		if (phys == 0 && curr_vaddr != 0) {
			auto mm = vma_find_contains(proc->vm_page, curr_vaddr);
			if (mm) {
				phys = mm->phys_address + (curr_vaddr - mm->start_address);
			}
		}

		if (phys != 0 || curr_vaddr == 0) {
			uint64_t old_entry = paging_get_entry(proc->page, curr_vaddr);
			uint64_t new_flags = mmap_flags;
			
			if (old_entry & PAGE_COW) {
				new_flags |= PAGE_COW;
				// If it's a COW page, we MUST NOT make it hardware-writable yet,
				// otherwise we bypass the COW mechanism and corrupt the parent's memory.
				// The actual write will trigger a page fault, and the COW handler
				// will allocate a new page and make it writable then.
				new_flags &= ~PAGE_WRITABLE;
			}
			
			paging_mmap(proc->page, curr_vaddr, phys, new_flags);
		}
	}

	paging_reload(proc->page); // Flush the TLB so the CPU sees the updated page permissions

	auto mm = vma_find_contains(proc->vm_page, (uintptr_t)addr);
	if (mm) {
		mm->flags = mmap_flags;
	}

	return 0;
}