#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/process.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include "sys/err_no.h"
#include <sys/syscall.h>

#define WNOHANG 1

int syscall_wait4(pid_t pid, int* wstatus, int options, void* rusage) {
	(void)rusage;
	auto thr = get_current_core_data()->active_thread;
	pid_t curr_pid = thr->process->pid;

	if (pid == (pid_t)-1 || pid == 4294967295) {
		serial2_printf("kondisi A\n");
		bool has_children = false;
		process_t* child = find_exited_child_process(curr_pid, &has_children);
		
		if (!child) {
		serial2_printf("kondisi B\n");

			if (!has_children) return -ECHILD;
			if (options & WNOHANG) return 0;
			thread_block();
			child = find_exited_child_process(curr_pid, &has_children);
			if (!child) return -EINTR;
		}
		
		if (wstatus) *wstatus = (child->exit_code & 0xff) << 8;
		pid_t exited_pid = child->pid;
		serial2_printf("destroying child pid : %d\n", child->pid);
		destroy_process(child);
		return (int)exited_pid;
	}

	if (pid <= 0) {
		serial2_printf("kondisi C\n");
		return -EINVAL;
	}

	process_t* child = find_process_by_pid(pid);
	if (!child) return -ECHILD;
	if (child->parent_pid != curr_pid) return -ECHILD;

	serial2_printf("found child %d at 0x%x\n", pid, child);

	bool exited = child->exited;
	if (!exited) {
		if (options & WNOHANG) return 0;
		thread_block();
		exited = child->exited;
	}

	if (!exited) return -EINTR;

	serial2_printf("child %d waited %d \n", pid, options);

	if (wstatus) *wstatus = (child->exit_code & 0xff) << 8;
	destroy_process(child);
	return (int)pid;
}
