#include "hal/cpu/core.h"
#include <cpu/irq_lock.h>
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "spinlock.h"
#include <str.h>
#include <string.h>
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

	if (new_brk > old_brk) {
		uintptr_t old_page = ALIGN_UP(old_brk, PAGE_SIZE_4KB);
		uintptr_t new_page = ALIGN_UP(new_brk, PAGE_SIZE_4KB);

		if (new_page > old_page) {
			void* phys = phys_base_alloc((new_page - old_page) / PAGE_SIZE_4KB);
			if (!phys) {
				serial2_printf(
				    "brk: phys_base_alloc failed for %d pages\n",
				    (new_page - old_page) / PAGE_SIZE_4KB);

				return (intptr_t)old_brk;
			}

			paging_multiple_mmap(proc->page, old_page, (uintptr_t)phys,
			               (new_page - old_page) / PAGE_SIZE_4KB,
			               PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

			vma_register(proc->vm_page, (uintptr_t)phys, old_page,
			             new_page - old_page,
			             PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER |
			                 PAGE_NO_EXECUTE);
			             
			size_t len_4kb = (new_page - old_page) / PAGE_SIZE_4KB;
			page_t kpml4 = paging_get_highest_page_map();
			uintptr_t temp_vaddr = vma_lookup_free_vaddr(get_kernel_vmm_page(), VMA_REGION_A, len_4kb);
			if (temp_vaddr) {
				vma_register(get_kernel_vmm_page(), (uintptr_t)phys, temp_vaddr, len_4kb * PAGE_SIZE_4KB,
				             PAGE_PRESENT | PAGE_WRITABLE | PAGE_NO_EXECUTE);
				paging_multiple_mmap(kpml4, temp_vaddr, (uintptr_t)phys, len_4kb, PAGE_PRESENT | PAGE_WRITABLE);
				
				paging_reload(kpml4);
				memset((void*)temp_vaddr, 0, len_4kb * PAGE_SIZE_4KB);
				
				paging_multiple_unmap(kpml4, temp_vaddr, len_4kb);
				vma_unregister(get_kernel_vmm_page(), temp_vaddr);
				paging_reload(proc->page);
			}
			
			serial2_printf("brk: from %x to %x\n", old_page, new_page);
		}
	}

	proc->heap_end = new_brk;

	serial2_printf("brk: returning %x\n", new_brk);

	return (intptr_t)new_brk;
}