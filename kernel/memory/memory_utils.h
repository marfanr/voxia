#ifndef __MEMORY__MEMORY_UTILS_H__
#define __MEMORY__MEMORY_UTILS_H__

#include <type.h>

#define ALIGN_UP(x, align)                                                     \
	(((x) + ((uintptr_t) (align) - 1)) & ~((uintptr_t) (align) - 1))

#define ALIGN_DOWN(x, align) ((x) & ~((uintptr_t) (align) - 1))

#define PHYS2VIRT(x) (uint64_t)((uint64_t) x + 0xffff800000000000UL)
#define VIRT2PHYS(x) (uint64_t)((uint64_t) x - 0xffff800000000000UL)

#define PTR_ADD(ptr, off) ((void*) (((uint8_t*) (ptr)) + (off)))

#define ASSUME_ALIGNED(ptr, align) __builtin_assume_aligned((ptr), (align))

#endif // __MEMORY__MEMORY_UTILS_H__