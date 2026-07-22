#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "str.h"
#include "vfs/vnode.h"
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>

long syscall_readv(int fd, const struct iovec* iov, int iovcnt) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;

	if (fd < 0 || fd >= (int)fdt->max_fds) {
		LOG2_ERROR("readv", "fd %d is invalid, max fd %d", fd,
		           fdt->max_fds);
		return -EBADF;
	}

	auto curr_fd = fdt->fds[fd];
	if (!curr_fd || !curr_fd->vnode) {
		LOG2_ERROR("readv", "fd %d vnode is missing", fd);
		return -EBADF;
	}

	auto ops = (vops_file_t*)curr_fd->ops;
	if (!ops || !ops->read) {
		LOG2_ERROR("readv", "fd %d read ops is missing", fd);
		return -ENOTTY;
	}

	if (!iov || iovcnt <= 0)
		return -EINVAL;

	auto iovec_ =
	    (struct iovec*)kalloc(sizeof(struct iovec) * (size_t)iovcnt);
	memcopy(iovec_, (void*)iov, sizeof(struct iovec) * (size_t)iovcnt);

	long total_read = 0;
	size_t current_offset = curr_fd->pos;
	
	for (int i = 0; i < iovcnt; i++) {
		auto iov_ = &iovec_[i];
		if (!iov_->iov_len || !iov_->iov_base)
			continue;

		serial2_printf("readv: to 0x%x len %d (thread %d)\n",
		               iov_->iov_base, iov_->iov_len,
		               get_current_core_data()->active_thread->id);

		void* kbuf = kalloc((size_t)iov_->iov_len);
		if (!kbuf) {
			kfree2(iovec_);
			return -ENOMEM;
		}

		int read_count =
		    ops->read(curr_fd->vnode, kbuf, (size_t)iov_->iov_len,
		              current_offset);

		if (read_count < 0) {
			kfree2(kbuf);
			kfree2(iovec_);
			return read_count;
		}

		if (read_count > 0) {
			paging_reload(curr_procc->page);
			memcopy(iov_->iov_base, kbuf, (size_t)read_count);
		}

		kfree2(kbuf);

		current_offset += (size_t)read_count;
		total_read += read_count;
		
		if (read_count < iov_->iov_len) {
			break;
		}
	}
	
	curr_fd->pos = (size_t)current_offset;

	kfree2(iovec_);
	return total_read;
}
