#ifndef __CPU__IRQ_H__
#define __CPU__IRQ_H__

#include <type.h>

static inline uintptr_t irq_save(void) {
	uintptr_t flags;
	__asm__ volatile(
		"pushfq         \n\t"
		"pop  %0        \n\t"
		"cli            \n\t"
		: "=r"(flags)
		:
		: "memory"
	);
	return flags;
}

static inline void irq_restore(uintptr_t flags) {
	__asm__ volatile(
		"push %0        \n\t"
		"popfq          \n\t"
		:
		: "r"(flags)
		: "memory", "cc"
	);
}

static inline void irq_enable(void) {
	__asm__ volatile("sti" ::: "memory");
}

static inline void irq_disable(void) {
	__asm__ volatile("cli" ::: "memory");
}

/* Check if interrupt is active (IF in RFLAGS) */
static inline bool irq_is_enabled(void) {
	uintptr_t flags;
	__asm__ volatile("pushfq \n\t"
			 "pop %0 \n\t"
			 : "=r"(flags));
	return (flags & (1UL << 9)) != 0; /* bit 9 = IF */
}

#endif // irq