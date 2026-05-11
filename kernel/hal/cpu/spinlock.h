#ifndef __HAL_CPU_SPINLOCK_H__
#define __HAL_CPU_SPINLOCK_H__

#include <type.h>

typedef struct spinlock {
	volatile int locked;
} spinlock_t;

void spin_acquire(spinlock_t* lock);
void spin_release(spinlock_t* lock);

#endif // __HAL_CPU_SPINLOCK_H__