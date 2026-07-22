#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/process.h"
#include "procc/scheduler.h"
#include "sys/sig.h"
#include <sys/syscall.h>

void syscall_exit_group(int status) {
	auto thr = get_current_core_data()->active_thread;
	if (!thr) {
		return;
	}

	auto proc = thr->process;
	if (!proc) {
		return;
	}

	serial2_printf("exit_group: status code %d\n", status);

	thr->state = THREAD_STATE_TERMINATED;

	if (thr->clear_child_tid) {
		serial2_printf("clearing tid %d..\n", *thr->clear_child_tid);
		*thr->clear_child_tid = 0;
		syscall_futex((int*)thr->clear_child_tid, 1 /* FUTEX_WAKE */, 1, NULL, NULL, 0);
	}

	// TODO: unmap all, remove process, and all thread inside

	auto parent_proc = find_process_by_pid(proc->parent_pid);
	if (parent_proc && parent_proc->signal) {
		sig_send(parent_proc->signal, SIGCHLD);
	}

	auto procc = thr->process;
	if (procc) {
		procc->exited = true;
		procc->exit_code = status;
		auto parent = find_process_by_pid(procc->parent_pid);
		if (parent && parent->main_thread) {
			serial2_printf(
			    "waking parent process %d (current %d)\n",
			    parent->pid, procc->pid);
			vxThreadWake(parent->main_thread);
		}
	}

	schedule_yield();

	serial2_printf("exit_group: should not reach here\n");
}