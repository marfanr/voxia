#ifndef __MEMORY__MEMORY_UTILS_H__
#define __MEMORY__MEMORY_UTILS_H__

#include <libk/type.h>

#define ALIGN_UP(x, align) (((x) + (align)-1) & ~((align)-1))
#define ALIGN_DOWN(x, align) ((x) & ~((align)-1))

#define PHYS2VIRT(x) (uint64_t) ((uint64_t)x + 0xffff800000000000UL)
#define VIRT2PHYS(x) (uint64_t) ((uint64_t)x - 0xffff800000000000UL)

#endif // __MEMORY__MEMORY_UTILS_H__