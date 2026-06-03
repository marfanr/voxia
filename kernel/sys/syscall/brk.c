#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "spinlock.h"
#include <sys/syscall.h>
intptr_t syscall_brk(void* addr) {
	auto curr_thr = get_current_core_data()->active_thread;
	auto proc = curr_thr->process;

	uintptr_t old_brk = proc->heap_end;

	if (!addr)
		return (intptr_t)old_brk;

	uintptr_t new_brk = (uintptr_t)addr;

	if (new_brk < proc->heap_start)
		return (intptr_t)old_brk;

	spin_acquire(&proc->vm_lock);

	if (new_brk > old_brk) {

		uintptr_t old_page = ALIGN_UP(old_brk, PAGE_SIZE);
		uintptr_t new_page = ALIGN_UP(new_brk, PAGE_SIZE);

		void* phys = phys_base_alloc((new_page - old_page) / PAGE_SIZE);
		if (!phys) {
			spin_release(&proc->vm_lock);
			return (intptr_t)old_brk;
		}
        
		vxMultipleMmap(curr_thr->page, old_page, (uintptr_t)phys,
        (new_page - old_page) / PAGE_SIZE,
        PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        
		vma_register(proc->vm_page, (uintptr_t)phys, old_page, new_page - old_page);
        serial2_printf("brk: from %x to %x\n", old_page, new_page);
	}

	proc->heap_end = new_brk;

	spin_release(&proc->vm_lock);

	return (intptr_t)new_brk;
}