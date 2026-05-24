#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include "vfs/vnode.h"
#include <sys/fd.h>
#include <sys/syscall.h>

int ioctl(int fd, uint32_t req, void* arg) {

	auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;
	auto curr_fd = fdt->fds[fd];

	if (fd < 0 || fd > (int)fdt->max_fds) {
		LOG2_ERROR("Ioctl", "fd %d is invalid, max fd %d", fd, fdt->max_fds);
		return -EBADF;
	}
	auto ops = (vops_file_t*)curr_fd->ops;
	if (!ops) {
		LOG2_ERROR("Ioctl", "fd %d ops is missing", fd);
		return -EBADF;
	}

	if (!curr_fd->vnode) {
		LOG2_ERROR("Ioctl", "fd %d vnode is missing", fd);
		return -EBADF;
	}

	if (!ops->ioctl) {
		LOG2_ERROR("Ioctl", "fd %d `ioctl` ops is missing", fd);
		return -ENOTTY;
	}

	return ops->ioctl(curr_fd->vnode, req, arg);
}