#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/process.h"
#include "procc/thread.h"
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>
#include <vfs/vnode.h>

extern struct file_descriptor* alloc_fd();

long syscall_dup(int oldfd) {
	auto thr = get_current_core_data()->active_thread;
	if (!thr || !thr->process || !thr->process->fdtable)
		return -EBADF;
	auto fdt = thr->process->fdtable;

	if (oldfd < 0 || oldfd >= (int)fdt->max_fds || !fdt->fds[oldfd]) {
		return -EBADF;
	}

	int newfd = 0;
	for (newfd = 0; newfd < (int)fdt->max_fds; newfd++) {
		if (fdt->fds[newfd] == nullptr)
			break;
	}
	if (newfd >= (int)fdt->max_fds)
		return -EMFILE;

	auto fd = fdt->fds[oldfd];
	__atomic_add_fetch(&fd->count.counter, 1, __ATOMIC_SEQ_CST);
	fdt->fd_flags[newfd] = 0; // Clear FD_CLOEXEC as required by POSIX
	fdt->fds[newfd] = fd;

	if (newfd >= (int)fdt->next_fd) {
		fdt->next_fd = (uint32_t)newfd + 1;
	}
	return newfd;
}

long syscall_dup2(int oldfd, int newfd) {
	serial2_printf("dup2: oldfd=%d newfd=%d\n", oldfd, newfd);
	auto thr = get_current_core_data()->active_thread;
	if (!thr || !thr->process || !thr->process->fdtable)
		return -EBADF;
	auto fdt = thr->process->fdtable;

	if (oldfd < 0 || oldfd >= (int)fdt->max_fds || !fdt->fds[oldfd]) {
		return -EBADF;
	}
	if (newfd < 0 || newfd >= (int)fdt->max_fds)
		return -EBADF;

	if (oldfd == newfd)
		return newfd;

	if (fdt->fds[newfd]) {
		extern int syscall_close(int fd);
		syscall_close(newfd);
	}

	auto fd = fdt->fds[oldfd];
	__atomic_add_fetch(&fd->count.counter, 1, __ATOMIC_SEQ_CST);
	fdt->fd_flags[newfd] = 0; // Clear FD_CLOEXEC
	fdt->fds[newfd] = fd;

	if (newfd >= (int)fdt->next_fd) {
		fdt->next_fd = (uint32_t)newfd + 1;
	}
	return newfd;
}
