#include "./spinlock.h"

void
spin_acquire(spinlock_t *lock)
{
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}

void
spin_release(spinlock_t *lock)
{
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
    __asm__ volatile("mfence");
}
