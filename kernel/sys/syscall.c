#include "syscall.h"
#include "hal/cpu/core.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "init/init.h"
#include "libk/serial.h"

// prototype
extern void syscall_dispatch(interrupt_stack_frame_t* rsp);
extern void syscall_entry(void);

static const char* get_syscall_name(int rax);
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
	if (rsp->rax != SYSCALL_EXIT)
		LOG2_DEBUG("syscall", "called %d (%s) %d 0x%x %d", rsp->rax,
		           get_syscall_name((int)rsp->rax), rsp->rdi, rsp->rsi,
		           rsp->rdx);

	auto int_no = rsp->rax;
	// SYSCALL_DEF(0x20, ())
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
	case SYSCALL_OPEN: {
		rsp->rax = (uint64_t)syscall_open((const char*)rsp->rdi,
		                                  (int)rsp->rsi, (int)rsp->rdx);
		break;
	}
	case SYSCALL_ARCH_PRCTL: {
		rsp->rax = (uint64_t)syscall_arch_prctl(
		    (int)rsp->rdi, (unsigned long)rsp->rsi);
		break;
	}
	case SYSCALL_SET_TID: {
		rsp->rax = (uint64_t)syscall_set_tid((uint32_t)rsp->rdi);
		break;
	}
	case SYSCALL_EXIT: {
		auto thr = get_current_core_data()->active_thread;
		thr->state = THREAD_STATE_TERMINATED;
		*thr->clear_child_tid = 0;
		break;
	}
	case SYSCALL_IOCTL: {
		rsp->rax = (uint64_t)ioctl((int)rsp->rdi, (uint32_t)rsp->rsi,
		                            (void*)rsp->rdx);
		break;
	}
	default:
		serial2_printf("unknown syscall %d\n", int_no);
		rsp->rax = (uint64_t)-1;
		break;
	}
}

INIT(Syscall) { syscall_init(); }

extern const char* syscall_names[335];
// Fungsi aman untuk mendapatkan nama syscall tanpa takut segmentation fault /
// array out-of-bounds
static const char* get_syscall_name(int rax) {
	// Menghitung ukuran array dinamis jika ada update di masa depan
	int max_syscalls = sizeof(syscall_names) / sizeof(syscall_names[0]);

	if (rax >= 0 && rax < max_syscalls && syscall_names[rax] != NULL) {
		return syscall_names[rax];
	}
	return "UNKNOWN_SYSCALL / NOT_IMPLEMENTED";
}