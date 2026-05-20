#ifndef __LIBK__ATOMIC_H__
#define __LIBK__ATOMIC_H__

#include <type.h>

static inline uint64_t
atomic_fetch_add_u64(volatile uint64_t *addr, uint64_t val)
{
    __asm__ volatile("lock; xaddq %0, %1" : "+r"(val), "+m"(*addr) : : "memory");
    return val;
}

#endif // __LIBK__ATOMIC_H__