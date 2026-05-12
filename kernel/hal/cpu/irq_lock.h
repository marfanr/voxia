#ifndef __HAL__CPU__IRQ_H__
#define __HAL__CPU__IRQ_H__

#include <type.h>

static inline uintptr_t irq_save(void) {
	uintptr_t flags;
	__asm__ volatile(
		"pushfq         \n\t" /* push RFLAGS ke stack             */
		"pop  %0        \n\t" /* baca ke 'flags'                  */
		"cli            \n\t" /* clear IF — disable interrupt     */
		: "=r"(flags)
		:
		: "memory" /* barrier: compiler tidak boleh
                                   reorder load/store melewati CLI  */
	);
	return flags;
}

static inline void irq_restore(uintptr_t flags) {
	__asm__ volatile(
		"push %0        \n\t" /* push nilai flags lama ke stack   */
		"popfq          \n\t" /* restore ke RFLAGS secara atomik  */
		:
		: "r"(flags)
		: "memory", "cc" /* cc: condition codes bisa berubah */
	);
}

static inline void irq_enable(void) {
	__asm__ volatile("sti" ::: "memory");
}

static inline void irq_disable(void) {
	__asm__ volatile("cli" ::: "memory");
}

/* Cek apakah interrupt sedang aktif (IF set di RFLAGS) */
static inline bool irq_is_enabled(void) {
	uintptr_t flags;
	__asm__ volatile("pushfq \n\t"
			 "pop %0 \n\t"
			 : "=r"(flags));
	return (flags & (1UL << 9)) != 0; /* bit 9 = IF */
}

#endif // irq