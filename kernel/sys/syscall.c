#include "syscall.h"
#include "hal/cpu/core.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/serial.h"
#include "sys/err_no.h"

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
		LOG2_DEBUG("syscall", "called %d (%s) 0x%x 0x%x %d", rsp->rax,
		           get_syscall_name((int)rsp->rax), rsp->rdi, rsp->rsi,
		           rsp->rdx);

	auto int_no = rsp->rax;

	// test
	// uint64_t cr3 = 0;
	// asm volatile("mov %%cr3, %0" : "=r"(cr3));

	// if (cr3 == (uint64_t)paging_get_highest_page_map()) {
	// 	serial2_printf("ini page kernel\n");
	// } else {
	// 	serial2_printf("ini page user\n");
	// }

	// TODO: refactor this using array of linker section
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
		// TODO: clear allocated memory on heap and mmap
		break;
	}
	case SYSCALL_IOCTL: {
		rsp->rax = (uint64_t)syscall_ioctl((int)rsp->rdi, (uint32_t)rsp->rsi,
		                           (void*)rsp->rdx);
		break;
	}
	case SYSCALL_WRITEV: {
		rsp->rax = (uint64_t)syscall_writev(
		    (int)rsp->rdi, (const struct iovec*)rsp->rsi,
		    (int)rsp->rdx);
		break;
	}
	case SYSCALL_BRK: {
		rsp->rax = (uint64_t)syscall_brk((void*)rsp->rdi);
		break;
	}
	case SYSCALL_MMAP: {
		rsp->rax = (uint64_t)syscall_mmap(
		    (void*)rsp->rdi, (size_t)rsp->rsi, (int)rsp->rdx, (int)rsp->r10,
		    (int)rsp->r8, (int)rsp->r9);
		break;
	}
	case SYSCALL_MPORTECT: {
		rsp->rax = (uint64_t)syscall_mprotect(
		    (void*)rsp->rdi, (size_t)rsp->rsi, (int)rsp->rdx);
		break;
	}
	default:
		serial2_printf("unknown syscall %d\n", int_no);
		rsp->rax = (uint64_t)-ENOSYS;
		break;
	}
}

INIT(Syscall) { syscall_init(); }

extern const char* syscall_names[335];
static const char* get_syscall_name(int rax) {
	int max_syscalls = sizeof(syscall_names) / sizeof(syscall_names[0]);

	if (rax >= 0 && rax < max_syscalls && syscall_names[rax] != NULL) {
		return syscall_names[rax];
	}
	return "UNKNOWN_SYSCALL / NOT_IMPLEMENTED";
}