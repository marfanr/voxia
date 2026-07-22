#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/process.h"
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>
#include <vfs/dentry.h>
#include <vfs/vnode.h>
#include <type.h>

/* Flag definitions — must match musl arch/x32/bits/fcntl.h */
#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_GETFL         3
#define F_SETFL         4
#define F_DUPFD_CLOEXEC 1030

#define FD_CLOEXEC      1

/* O_ flags — must match open.c definitions */
#define O_APPEND        02000
#define O_NONBLOCK      04000
#define O_DSYNC         010000
#define O_SYNC          04010000
#define O_RSYNC         04010000
#define O_ASYNC         020000
#define O_DIRECT        040000
#define O_NOATIME       01000000

/*
 * Flags allowed to be changed via F_SETFL.
 * Linux only allows: O_APPEND | O_NONBLOCK | O_ASYNC | O_DIRECT | O_NOATIME.
 */
#define SETFL_ALLOWED   (O_APPEND | O_NONBLOCK | O_ASYNC | O_DIRECT | O_NOATIME)

long syscall_fcntl(int fd, int cmd, unsigned long arg) {
	auto thr = get_current_core_data()->active_thread;
	if (!thr || !thr->process || !thr->process->fdtable)
		return -EBADF;

	auto fdt = thr->process->fdtable;

	if (fd < 0 || fd >= (int)fdt->max_fds || !fdt->fds[fd])
		return -EBADF;

	auto file = fdt->fds[fd];

	switch (cmd) {
	case F_DUPFD: {
		/* Duplicate fd, starting at arg (lowest available >= arg) */
		int start = (int)arg;
		if (start < 0)
			start = 0;

		int newfd;
		for (newfd = start; newfd < (int)fdt->max_fds; newfd++) {
			if (!fdt->fds[newfd])
				break;
		}
		if (newfd >= (int)fdt->max_fds)
			return -EMFILE;

		auto new_file = alloc_fd();
		memcopy(new_file, file, sizeof(struct file_descriptor));
		if (new_file->dentry) {
			dentry_get(new_file->dentry);
		}
		if (new_file->vnode && new_file->vnode->type == VNODE_TYPE_FIFO) {
			extern void pipe_dup_fd(struct file_descriptor* fd);
			pipe_dup_fd(new_file);
		}
		fdt->fds[newfd] = new_file;

		if (newfd >= (int)fdt->next_fd)
			fdt->next_fd = (uint32_t)newfd + 1;

		return newfd;
	}

	case F_DUPFD_CLOEXEC: {
		/* Same as F_DUPFD but set close-on-exec. */
		int start = (int)arg;
		if (start < 0)
			start = 0;

		int newfd;
		for (newfd = start; newfd < (int)fdt->max_fds; newfd++) {
			if (!fdt->fds[newfd])
				break;
		}
		if (newfd >= (int)fdt->max_fds)
			return -EMFILE;

		auto new_file = alloc_fd();
		memcopy(new_file, file, sizeof(struct file_descriptor));
		new_file->fd_flags = FD_CLOEXEC;
		if (new_file->dentry) {
			dentry_get(new_file->dentry);
		}
		if (new_file->vnode && new_file->vnode->type == VNODE_TYPE_FIFO) {
			extern void pipe_dup_fd(struct file_descriptor* fd);
			pipe_dup_fd(new_file);
		}
		fdt->fds[newfd] = new_file;

		if (newfd >= (int)fdt->next_fd)
			fdt->next_fd = (uint32_t)newfd + 1;

		return newfd;
	}

	case F_GETFD:
		return file->fd_flags;

	case F_SETFD:
		file->fd_flags = (uint32_t)arg;
		return 0;

	case F_GETFL:
		/* Return file status flags that were set at open(). */
		return (long)(file->flags);

	case F_SETFL: {
		/* Only allow changing certain flags. Preserve O_RDONLY/WRONLY/RDWR
		 * and other read-only flags, only modify allowed mutable flags. */
		uint32_t old_flags = file->flags;
		uint32_t new_flags = (uint32_t)arg;

		/* Keep access mode (lower 2 bits) and other immutable flags */
		uint32_t keep = old_flags & (uint32_t)~SETFL_ALLOWED;
		uint32_t changed = new_flags & SETFL_ALLOWED;

		file->flags = keep | changed;
		serial2_printf("F_SETFL fd=%d old=0x%x new=0x%x flags=0x%x\n", fd, old_flags, new_flags, file->flags);
		return 0;
	}

	default:
		serial2_printf("fcntl: unknown cmd %d (fd=%d, arg=0x%lx)\n",
		               cmd, fd, arg);
		return -EINVAL;
	}
}
