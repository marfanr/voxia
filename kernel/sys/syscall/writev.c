#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "str.h"
#include "vfs/vnode.h"
#include <sys/err_no.h>
#include <sys/fd.h>
#include <sys/syscall.h>

long syscall_writev(int fd, const struct iovec* iov, int iovcnt) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;

	if (fd < 0 || fd >= (int)fdt->max_fds) {
		LOG2_ERROR("writev", "fd %d is invalid, max fd %d", fd,
		           fdt->max_fds);
		return -EBADF;
	}

	auto curr_fd = fdt->fds[fd];
	if (!curr_fd || !curr_fd->vnode) {
		LOG2_ERROR("writev", "fd %d vnode is missing", fd);
		return -EBADF;
	}

	auto ops = (vops_file_t*)curr_fd->ops;
	if (!ops || !ops->write) {
		LOG2_ERROR("writev", "fd %d write ops is missing", fd);
		return -ENOTTY;
	}

	if (!iov || iovcnt <= 0)
		return -EINVAL;

	auto iovec_ =
	    (struct iovec*)kalloc(sizeof(struct iovec) * (size_t)iovcnt);
	memcopy(iovec_, (void*)iov, sizeof(struct iovec) * (size_t)iovcnt);

	long total_written = 0;
	bool is_file = (curr_fd->vnode->type == VNODE_TYPE_FILE && curr_fd->vnode->fs_instance);

	for (int i = 0; i < iovcnt; i++) {
		auto iov_ = &iovec_[i];
		if (!iov_->iov_len || !iov_->iov_base)
			continue;

		size_t count = (size_t)iov_->iov_len;
		void* buf = iov_->iov_base;

		if (is_file) {
			if (!curr_fd->write_buffer) {
				curr_fd->write_buffer = kalloc(4096);
				if (!curr_fd->write_buffer) {
					kfree2(iovec_);
					return total_written > 0 ? total_written : -ENOMEM;
				}
				memset(curr_fd->write_buffer, 0, 4096);
				curr_fd->write_buffer_size = 0;
			}

			while (count > 0) {
				if (curr_fd->write_buffer_size == 4096) {
					auto s = ops->write(curr_fd->vnode, curr_fd->write_buffer,
					                    curr_fd->write_buffer_size,
					                    curr_fd->pos - curr_fd->write_buffer_size);
					if (s == 0 && curr_fd->write_buffer_size > 0) {
						kfree2(iovec_);
						return total_written > 0 ? total_written : -ENOSPC;
					}
					curr_fd->write_buffer_size = 0;
				}

				if (curr_fd->write_buffer_size == 0 && count >= 4096) {
					auto s = ops->write(curr_fd->vnode, buf, count, curr_fd->pos);
					if (s > 0) {
						curr_fd->pos += (size_t)s;
						total_written += s;
						count -= (size_t)s;
						buf = (void*)((uint8_t*)buf + s);
					} else if (s == 0) {
						kfree2(iovec_);
						return total_written > 0 ? total_written : -ENOSPC;
					} else {
						kfree2(iovec_);
						return total_written > 0 ? total_written : s;
					}
					continue;
				}

				size_t to_copy = 4096 - curr_fd->write_buffer_size;
				if (to_copy > count)
					to_copy = count;

				memcopy((uint8_t*)curr_fd->write_buffer + curr_fd->write_buffer_size,
				        buf, to_copy);

				curr_fd->write_buffer_size += to_copy;
				curr_fd->pos += to_copy;
				total_written += to_copy;
				count -= to_copy;
				buf = (void*)((uint8_t*)buf + to_copy);
			}
		} else {
			long write_count = ops->write(curr_fd->vnode, buf, count, (size_t)curr_fd->pos);
			if (write_count < 0) {
				kfree2(iovec_);
				return total_written > 0 ? total_written : write_count;
			} else if (write_count == 0 && count > 0) {
				if (total_written == 0) {
					kfree2(iovec_);
					return -ENOSPC;
				}
				break;
			}

			curr_fd->pos += (size_t)write_count;
			total_written += write_count;
		}
	}

	kfree2(iovec_);
	serial2_printf("total wrutten %d byte\n", total_written);
	return total_written;
}