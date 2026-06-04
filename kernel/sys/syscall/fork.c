#include "hal/cpu/core.h"
#include "procc/thread.h"
#include <sys/syscall.h>

int syscall_fork(void) {
	// syscall_fork()
	auto parent = get_current_core_data()->active_thread;

	auto child = fork(parent, parent->reg.rip);

	if (!child) {
		return -1;
	}

	interrupt_stack_frame_t* child_rsp = 
        (interrupt_stack_frame_t*)child->kernel_rsp;
    child_rsp->rax = 0;

	return (int)child->process->pid;
}