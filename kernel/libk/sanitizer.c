#include "console/console.h"
#include "libk/serial.h"
#include <type.h>
#include <type.h>

uintptr_t __stack_chk_guard = 0x595e9fbd94fda766;

__attribute__((noreturn, used, visibility("default"), section(".export"))) void
__stack_chk_fail(void);

typedef struct stackframe {
	struct stackframe* rbp;
	uint64_t rip;
} stackframe_t;

static void stacktrace(void) {
	stackframe_t* frame;

	asm volatile("mov %%rbp, %0" : "=r"(frame));

	while (frame) {
		serial2_printf("0x%p ", frame->rip);
		frame = frame->rbp;
	}
}

__attribute__((noreturn, used, visibility("default"), section(".export"))) void
__stack_chk_fail(void) {
	serial2_printf("STACK CORRUPTION DETECTED\n");
	console_printf("STACK CORRUPTION DETECTED\n");

	stacktrace();

	while (1) {
		__asm__ volatile("cli");
		__asm__ volatile("hlt");
	}
}