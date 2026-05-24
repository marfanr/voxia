#include "hal/cpu/core.h"
#include "libk/serial.h"
#include <sys/syscall.h>

pid_t syscall_set_tid(uint32_t tid) {
	// auto thr = get_current_core_data()->active_thread;
	// thr->tid = tid;
	serial2_printf("set_tid: 0x%x\n", tid);

	// TODO: check tid is user space memory
	auto curr_thread = get_current_core_data()->active_thread;
	curr_thread->clear_child_tid = (uint32_t*)(uintptr_t)tid;

	return curr_thread->process->pid;
}