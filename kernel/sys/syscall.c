#include "syscall.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "init/init.h"
#include "libk/serial.h"

// prototype
extern void syscall_dispatch(interrupt_stack_frame_t* rsp);
extern void syscall_entry(void);

void syscall_init(void) {
	uint64_t efer = vxRDMSR(MSR_EFER);
	vxWRSR(MSR_EFER, efer | 1); // SCE bit

	vxWRSR(MSR_LSTAR, (uint64_t)syscall_entry);

	uint64_t star = 0;
	star |= (uint64_t)0x0028 << 32; // kernel CS (0x28), kernel SS (0x30)
	star |= (uint64_t)0x003B << 48; // user base for SYSRET (CS32 = 0x38, SS
	                                // = +8 = 0x40, CS64 = +16 = 0x48)
	vxWRSR(MSR_STAR, star);

	vxWRSR(MSR_FMASK, (1 << 9)); // IF bit
}

extern void syscall_dispatch(interrupt_stack_frame_t* rsp) {
	// #DEBUG
	LOG2_DEBUG("syscall", "called %d %d %x %d", rsp->rax, rsp->rdi,
	               rsp->rsi, rsp->rdx);

	auto int_no = rsp->rax;
	switch (int_no) {
	case SYSCALL_READ:
		rsp->rax = (uint64_t)syscall_read(
		    (int)rsp->rdi, (void*)rsp->rsi, (int)rsp->rdx);
		break;
	case SYSCALL_WRITE: {
		rsp->rax = (uint64_t)syscall_write(
		    (int)rsp->rdi, (void*)rsp->rsi, (int)rsp->rdx);
		break;
	}
	default:
		serial2_printf("unknown syscall %d\n", int_no);
		rsp->rax = (uint64_t)-1;
		break;
	}
}

INIT(Syscall) { syscall_init(); }