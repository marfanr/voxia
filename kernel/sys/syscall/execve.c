// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Mohammad Arfan

#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/process.h"
#include "string.h"
#include "vfs/dentry.h"
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>

#define DEBUG 1

int syscall_execve(const char* path, char* const argv[], char* const envp[], interrupt_stack_frame_t* rsp) {
	auto thread = get_current_core_data()->active_thread;
	if (!thread) {
		return -1;
	}

	serial2_printf("syscall_execve %s\n", path);

	auto proc = thread->process;
	if (!proc) {
		return -1;
	}

	char** safe_argv = copy_string_array(argv);
	char** safe_envp = copy_string_array(envp);
	kstring path_copy = safe_str_from_user(proc->page, path);

	if (!path_copy || !safe_argv || (envp && !safe_envp)) {
		if (safe_argv)
			free_string_array(safe_argv);
		if (safe_envp)
			free_string_array(safe_envp);
		if (path_copy)
			str_release(path_copy);
		return -EFAULT;
	}

	for (int i = 0; safe_argv[i]; i++) {
		serial2_printf("argv[%d] %s\n", i, safe_argv[i]);
	}

	int ret = run_process_at_proc(path_copy->c_str, safe_argv, safe_envp, proc, rsp);

	/* reset signal handlers: caught signals → SIG_DFL, ignored tetap SIG_IGN */
	if (ret >= 0 && thread->signal) {
		for (int sig = 1; sig <= MAX_SIGNAL_AVAILABLE; sig++) {
			sig_handle_ptr_t h =
			    thread->signal->handler[sig - 1];
			if (h != 0 && (uintptr_t)h != 1)
				thread->signal->handler[sig - 1] =
				    0; /* SIG_DFL */
		}
	}

	if (ret >= 0) {
		auto fdt = proc->fdtable;
		if (fdt) {
			for (uint32_t i = 0; i < fdt->max_fds; i++) {
				if (fdt->fds[i] && (fdt->fd_flags[i] & 1 /* FD_CLOEXEC */)) {
					syscall_close((int)i);
				}
			}
		}
	}

	free_string_array(safe_argv);
	free_string_array(safe_envp);

#if DEBUG
	serial2_printf("executing %s (pid %d)\n", path_copy->c_str, proc->pid);
#endif

	print_dentry_tree(get_root_dentry(), 0);

	str_release(path_copy);

	return ret < 0 ? ret : 0;
}
#undef DEBUG