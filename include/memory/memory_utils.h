#ifndef __MEMORY__MEMORY_UTILS_H__
#define __MEMORY__MEMORY_UTILS_H__

#include <type.h>

#define ALIGN_UP(x, align)                                                     \
	(((x) + ((uintptr_t) (align) - 1)) & ~((uintptr_t) (align) - 1))

#define ALIGN_DOWN(x, align) ((x) & ~((uintptr_t) (align) - 1))

extern uint64_t g_hhdm_offset;
/* WARNING: These macros depend on a global HHDM mapping which has been removed. 
   Only use during early boot or for physical address calculations from bootloader pointers. 
   After paging is set, use physical windows for accessing arbitrary physical memory. */
#define PHYS2VIRT(x) (uint64_t)((uint64_t)(x) + g_hhdm_offset)
#define VIRT2PHYS(x) (uint64_t)((uint64_t)(x) - g_hhdm_offset)

#define PTR_ADD(ptr, off) ((void*) (((uint8_t*) (ptr)) + (off)))

#define ASSUME_ALIGNED(ptr, align) __builtin_assume_aligned((ptr), (align))

#endif // __MEMORY__MEMORY_UTILS_H__