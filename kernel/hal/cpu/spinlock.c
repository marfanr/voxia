#include "./spinlock.h"

void spin_acquire(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, 1))
        __builtin_ia32_pause();
}

void spin_release(spinlock_t *lock) { __sync_lock_release(&lock->locked); }
