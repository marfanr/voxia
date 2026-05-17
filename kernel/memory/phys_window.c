#include "phys_window.h"
#include "autoconf.h"
#include <hal/cpu/paging.h>
#include <libk/serial.h>
#include <memory/vm_manager.h>

static mem_physwindow_t physical_memory_windows[VOXIA_PHYS_MAX_WINDOW_COUNT] = {
	0};

extern void paging_physwindow_mmap(page_t page_dir, uint64_t virt,
				   uint64_t phys, int flags);

/**
 * resolve a physical memory window by virtual address
 * @param virt_addr the virtual address of the physical memory window
 * @return a pointer to the mem_physwindow_t structure if found, NULL otherwise
 */
mem_physwindow_t* mem_resolve_physwindow(uintptr_t virt_addr) {
	for (size_t i = 0; i < VOXIA_PHYS_MAX_WINDOW_COUNT; i++) {
		if (physical_memory_windows[i].virt_addr == virt_addr) {
			return &physical_memory_windows[i];
		}
	}
	return 0;
}

mem_physwindow_status_t
mem_create_physwindow(uintptr_t phys_addr, uintptr_t* virt_addr,
		      mem_physwindow_flag_t flag) {
	for (size_t i = 0; i < VOXIA_PHYS_MAX_WINDOW_COUNT; i++) {
		if (!physical_memory_windows[i].lock) {
			*virt_addr = mem_vma_phys_window_start + i * 0x1000;
			physical_memory_windows[i].virt_addr = *virt_addr;
			physical_memory_windows[i].phys_addr = phys_addr;
			physical_memory_windows[i].flag = flag;

			paging_physwindow_mmap(
				paging_get_highest_page_map(), *virt_addr,
				phys_addr,
				(flag == PHYS_WINDOW_FLAG_READ ? 0b1 : 0)
					| (flag == PHYS_WINDOW_FLAG_WRITE ? 0b10
									  : 0b0)
					| 0b11);

			if ((flag & PHYS_WINDOW_FLAG_LOCK)
			    == PHYS_WINDOW_FLAG_LOCK) {
				physical_memory_windows[i].lock = 1;
			}

			// serial_trace("mem_create_physwindow: Created physical window at %d\n", i);

			return PHYS_WINDOW_STATUS_OK;
		}
	}
	serial_trace("[ERROR] mem_create_physwindow: No available physical "
		     "window slots\n");
	return PHYS_WINDOW_STATUS_NOT_FOUND;
}

mem_physwindow_status_t mem_release_physwindow(uintptr_t virt_addr) {
	for (size_t i = 0; i < VOXIA_PHYS_MAX_WINDOW_COUNT; i++) {
		if (physical_memory_windows[i].virt_addr == virt_addr) {
			physical_memory_windows[i].lock = 0;
			return PHYS_WINDOW_STATUS_OK;
		}
	}

#if VOXIA_LOG_VERBOSE
	serial_trace("[WARNING] mem_release_physwindow: Virtual address 0x%x "
		     "not found in physical "
		     "memory windows\n",
		     virt_addr);
#endif
	return PHYS_WINDOW_STATUS_NOT_FOUND;
}
