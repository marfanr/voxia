#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/thread.h"
#include <sys/syscall.h>

int syscall_fork(interrupt_stack_frame_t* rsp) {
	// syscall_fork()
	auto parent = get_current_core_data()->active_thread;

	auto child = fork_process(parent, rsp);
	serial2_printf("Fork child : %d\n", child->process->pid);

	if (!child) {
		return -1;
	}

	interrupt_stack_frame_t* child_frame = 
        (interrupt_stack_frame_t*)child->kernel_rsp;
    child_frame->rax = 0;

	// Flush TLB to ensure parent triggers COW faults on writes
	uintptr_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	__asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");

	serial2_printf("FORK PARENT PID: %d, CHILD PID: %d\n", parent->process->pid, child->process->pid); 
	return (int)child->process->pid;
}