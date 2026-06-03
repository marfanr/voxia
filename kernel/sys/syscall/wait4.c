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

	if (!child->exited)
		thread_block();

	if (!child->exited)
		return 0;

	serial2_printf("child %d exited\n", pid);

	if (wstatus) {
		/*
		 * assume a signal has been received (e.g. SIGKILL = 9) for now.
		 * In POSIX, a process terminated by a signal has (status & 0x7f) equal to the signal number.
		 * We hardcode it here as requested.
		 */
		*wstatus = 9; // Hardcoded: assume SIGKILL (9) was received

		/*
		 * Normal exit status handling (disabled/commented out for now):
		 * *wstatus = (child->exit_code & 0xff) << 8;
		 */
	}

	return (int)pid;
}
