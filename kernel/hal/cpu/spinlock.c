#include "./spinlock.h"

static inline void cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
	/*
     * PAUSE memberi tahu CPU bahwa ini spin-wait:
     *  - Flush speculative load pipeline → kurangi memory bus thrashing
     *  - Turunkan konsumsi daya selama spinning
     *  - Hindari memory order violation penalty saat lock dilepas
     * Tanpa PAUSE, throughput multi-core bisa turun 10-40%.
     */
	__asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__)
	/* YIELD: hint ke CPU untuk de-prioritaskan thread ini */
	__asm__ volatile("yield" ::: "memory");
#elif defined(__riscv)
	/* Zihintpause extension; fallback ke nop jika tidak ada */
	__asm__ volatile(".option push; .option arch, +zihintpause; pause; "
			 ".option pop" ::
				 : "memory");
#else
	__asm__ volatile("" ::: "memory");
#endif
}

void spin_acquire(spinlock_t* lock) {

	uint16_t my_ticket =
		__atomic_fetch_add(&lock->next_ticket, 1, __ATOMIC_RELAXED);

	while (__atomic_load_n(&lock->now_serving, __ATOMIC_ACQUIRE)
	       != my_ticket) {
		cpu_relax();
	}
}

/* ------------------------------------------------------------------ */
/*  spin_release                                                         */
/* ------------------------------------------------------------------ */

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
