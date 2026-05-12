#ifndef __MEMORY__PHYS_WINDOW_H__
#define __MEMORY__PHYS_WINDOW_H__

#include <hal/cpu/spinlock.h>
#include <type.h>

typedef int mem_physwindow_status_t;
enum {
	PHYS_WINDOW_STATUS_OK = 0,
	PHYS_WINDOW_STATUS_ERROR = -1,
	PHYS_WINDOW_STATUS_NOT_FOUND = -2,
} __attribute__((enum_extensibility(closed)));

typedef int mem_physwindow_flag_t;
enum {
	PHYS_WINDOW_FLAG_READ = 0x1,
	PHYS_WINDOW_FLAG_WRITE = 0x2,
	PHYS_WINDOW_FLAG_LOCK = 0x20,
} __attribute__((enum_extensibility(closed)));

typedef struct {
	uintptr_t virt_addr;
	uintptr_t phys_addr;
	_Atomic int lock;
	mem_physwindow_flag_t flag;
} __attribute__((aligned(32))) mem_physwindow_t;

mem_physwindow_status_t
mem_create_physwindow(uintptr_t phys_addr, uintptr_t* virt_addr,
		      mem_physwindow_flag_t flag);
mem_physwindow_status_t mem_release_physwindow(uintptr_t virt_addr);
mem_physwindow_t* mem_resolve_physwindow(uintptr_t virt_addr);

#endif // __MEMORY__PHYS_WINDOW_H__
