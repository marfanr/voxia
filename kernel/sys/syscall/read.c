#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include "vfs/vnode.h"
#include <sys/syscall.h>
#include <dev/event.h>
#include <sys/fd.h>

int syscall_read(int fd, void* buf, long count) {
    auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;

	if (fd < 0 || fd >= (int)fdt->max_fds) {
		LOG2_ERROR("read", "fd %d is invalid, max fd %d", fd,
		           fdt->max_fds);
		return -EBADF;
	}

	auto curr_fd = fdt->fds[fd];
	if (!curr_fd || !curr_fd->vnode) {
		LOG2_ERROR("read", "fd %d vnode is missing", fd);
		return -EBADF;
	}

	auto ops = (vops_file_t*)curr_fd->ops;
	if (!ops) {
		LOG2_ERROR("read", "fd %d ops is missing", fd);
		return -EBADF;
	}

	if (!ops->read) {
		LOG2_ERROR("read", "fd %d `read` ops is missing", fd);
		return -ENOTTY;
	}

	return ops->read(curr_fd->vnode, buf, (size_t)count, 0);
}