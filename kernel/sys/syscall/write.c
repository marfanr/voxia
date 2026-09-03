#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "net/socket.h"
#include "str.h"
#include "sys/err_no.h"
#include "vfs/vnode.h"
#include <string.h>
#include <sys/fd.h>
#include <sys/syscall.h>

int syscall_write(int fd, void* buf, long count) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;

	if (fd < 0 || fd >= (int)fdt->max_fds) {
		LOG2_ERROR("write", "fd %d is invalid, max fd %d", fd,
		           fdt->max_fds);
		return -EBADF;
	}

	auto curr_fd = fdt->fds[fd];
	if (!curr_fd) {
		LOG2_ERROR("write", "fd %d is missing", fd);
		return -EBADF;
	}

	/* Route socket writes to sendto */
	if (!curr_fd->vnode && curr_fd->private_data) {
		auto sock = (socket_t*)curr_fd->private_data;
		if (sock->family == AF_UNIX || sock->family == AF_INET) {
			return syscall_sendto(fd, buf, (uint32_t)count, 0, NULL, 0);
		}
	}

	if (!curr_fd->vnode) {
		LOG2_ERROR("write", "fd %d vnode is missing", fd);
		return -EBADF;
	}

	auto ops = (vops_file_t*)curr_fd->ops;
	if (!ops) {
		LOG2_ERROR("write", "fd %d ops is missing", fd);
		return -EBADF;
	}

	if (!ops->write) {
		LOG2_ERROR("write", "fd %d `write` ops is missing", fd);
		return -ENOTTY;
	}

	if (curr_fd->vnode->type == VNODE_TYPE_FILE &&
	    curr_fd->vnode->fs_instance) {
		if (!curr_fd->write_buffer) {
			curr_fd->write_buffer = kalloc(4096);
			if (!curr_fd->write_buffer) {
				return -ENOMEM;
			}
			memset(curr_fd->write_buffer, 0, 4096);
			curr_fd->write_buffer_size = 0;
		}

		if (curr_fd->write_buffer_size + (size_t)count > 4096) {
			auto s = ops->write(curr_fd->vnode, curr_fd->write_buffer,
			                    curr_fd->write_buffer_size,
			                    curr_fd->pos - curr_fd->write_buffer_size);
			if (s == 0 && curr_fd->write_buffer_size > 0) {
				return -ENOSPC;
			}
			curr_fd->write_buffer_size = 0;
		}

		if (count > 4096) {
			auto s = ops->write(curr_fd->vnode, buf, (size_t)count,
			                    curr_fd->pos);
			if (s > 0) {
				curr_fd->pos += (size_t)s;
			} else if (s == 0 && count > 0) {
				return -ENOSPC;
			}
			return (int)s;
		}

		memcopy((uint8_t*)curr_fd->write_buffer +
		            curr_fd->write_buffer_size,
		        buf, (size_t)count);

		curr_fd->write_buffer_size += (size_t)count;
		curr_fd->pos += (size_t)count;
		return (int)count;
	}

	auto s = ops->write(curr_fd->vnode, buf, (size_t)count, curr_fd->pos);
	if (s > 0)
		curr_fd->pos += (size_t)s;
	return (int)s;
}