#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "str.h"
#include "vfs/vnode.h"
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>

long syscall_writev(int fd, const struct iovec* iov, int iovcnt) {
	// TODO: handle is fd is not found

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

	if (!iov) {
		LOG2_ERROR("writev", "empty iovec");
		return -EINVAL;
	}

	if (!ops->write) {
		LOG2_ERROR("writev", "fd %d `write` ops is missing", fd);
		return -ENOTTY;
	}

	auto iovec_ =
	    (struct iovec*)kalloc(sizeof(struct iovec) * (size_t)iovcnt);
	memcopy(iovec_, (void*)iov, sizeof(struct iovec) * (size_t)iovcnt);

	long total_read = 0;
	for (int i = 0; i < iovcnt; i++) {
		auto iov_ = &iovec_[i];
		if (!iov->iov_len || !iov->iov_base)
			continue;

		serial2_printf("write: from 0x%x\n", iov_->iov_base);
		auto write_count =
		    ops->write(curr_fd->vnode, iov_->iov_base,
		               (size_t)iov_->iov_len, (size_t)total_read);

		if (write_count < 0)
			return -1;
        
        total_read += write_count;
	}
	return total_read;
}