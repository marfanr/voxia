#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include "vfs/vnode.h"
#include <sys/fd.h>
#include <sys/syscall.h>

int syscall_write(int fd, void* buf, long count) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;
	auto curr_fd = fdt->fds[fd];

	if (fd < 0 || fd > (int)fdt->max_fds) {
		LOG2_ERROR("writev", "fd %d is invalid, max fd %d", fd,
		           fdt->max_fds);
		return -EBADF;
	}

	auto ops = (vops_file_t*)curr_fd->ops;
	if (!ops) {
		LOG2_ERROR("writev", "fd %d ops is missing", fd);
		return -EBADF;
	}

	if (!curr_fd->vnode) {
		LOG2_ERROR("writev", "fd %d vnode is missing", fd);
		return -EBADF;
	}

	if (!ops->write) {
		LOG2_ERROR("writev", "fd %d `write` ops is missing", fd);
		return -ENOTTY;
	}

	auto s = ops->write(curr_fd->vnode, buf, (size_t)count, 0);
	return (int)s;
}