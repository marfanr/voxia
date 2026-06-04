#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/process.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include "sys/err_no.h"
#include <sys/syscall.h>

int syscall_wait4(pid_t pid, int* wstatus, int options, void* rusage) {
	(void)options;
	(void)rusage;

	if (pid <= 0) {
		return -EINVAL;
	}

	process_t* child = find_process_by_pid(pid);
	if (!child) {
		return -ECHILD;
	}

	auto thr = get_current_core_data()->active_thread;
	if (child->parent_pid != thr->process->pid)
		return -ECHILD;

	serial2_printf("found child %d at 0x%x\n", pid, child);

	spin_acquire(&child->lock);
	if (!child->exited)
		thread_block();

	if (!child->exited)
		return 0;
	spin_release(&child->lock);


	serial2_printf("child %d waited %d \n", pid, options);

	if (wstatus) {
		*wstatus = (child->exit_code & 0xff) << 8;
	}

	return (int)pid;
}
