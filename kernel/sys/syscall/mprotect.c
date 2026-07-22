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
	serial2_printf("mrpotect: base aligned 0x%x\n", addr_4kb);

	auto mm = vma_find_contains(proc->vm_page, (uintptr_t)addr);
	if (!mm) {
		return -1;
	}

	uintptr_t phys_offset = (uintptr_t)addr - mm->start_address;
	auto len_4kb = ALIGN_UP(len, 0x1000) / 0x1000;
	paging_multiple_mmap(proc->page, (uint64_t)addr, (uint64_t)(mm->phys_address + phys_offset),
	               len_4kb, mmap_flags);

	return 0;
}