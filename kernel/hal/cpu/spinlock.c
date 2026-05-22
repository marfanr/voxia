#include "./spinlock.h"

static inline void cpu_relax(void) {
	__asm__ volatile("pause" ::: "memory");
	__asm__ volatile("" ::: "memory");
}

void spin_acquire(spinlock_t* lock) {

	uint16_t my_ticket =
	    __atomic_fetch_add(&lock->next_ticket, 1, __ATOMIC_RELAXED);

	while (__atomic_load_n(&lock->now_serving, __ATOMIC_ACQUIRE) !=
	       my_ticket) {
		cpu_relax();
	}
}

void spin_release(spinlock_t* lock) {

	uint16_t next =
	    __atomic_load_n(&lock->now_serving, __ATOMIC_RELAXED) + 1;

	__atomic_store_n(&lock->now_serving, next, __ATOMIC_RELEASE);
}

bool spin_is_locked(const spinlock_t* lock) {
	uint16_t serving =
	    __atomic_load_n(&lock->now_serving, __ATOMIC_RELAXED);
	uint16_t next = __atomic_load_n(&lock->next_ticket, __ATOMIC_RELAXED);
	return serving != next;
}
