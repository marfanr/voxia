// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Mohammad Arfan

#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include "vfs/vnode.h"
#include <sys/fd.h>
#include <sys/syscall.h>
#include <type.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

long syscall_lseek(int fd, long offset, int whence) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;

	if (fd < 0 || fd >= (int)fdt->max_fds) {
		LOG2_ERROR("lseek", "fd %d is invalid, max fd %d", fd,
		           fdt->max_fds);
		return -EBADF;
	}

	auto curr_fd = fdt->fds[fd];
	if (!curr_fd || !curr_fd->vnode) {
		LOG2_ERROR("lseek", "fd %d vnode is missing", fd);
		return -EBADF;
	}

	// Sockets dan pipes/FIFOs are not supported
	if (curr_fd->vnode->type == VNODE_TYPE_FIFO ||
	    curr_fd->vnode->type == VNODE_TYPE_SOCK) {
		return -ESPIPE;
	}

	if (curr_fd->write_buffer && curr_fd->write_buffer_size > 0 &&
	    curr_fd->vnode->type == VNODE_TYPE_FILE) {
		auto ops = (vops_file_t*)curr_fd->ops;
		if (ops && ops->write) {
			long s = ops->write(
			    curr_fd->vnode, curr_fd->write_buffer,
			    curr_fd->write_buffer_size,
			    curr_fd->pos - curr_fd->write_buffer_size);
			if (s <= 0 && curr_fd->write_buffer_size > 0) {
				return s < 0 ? s : -EIO;
			}
		}
		curr_fd->write_buffer_size = 0;
	}

	long new_pos = 0;
	switch (whence) {
	case SEEK_SET: {
		if (offset < 0) {
			return -EINVAL;
		}
		new_pos = offset;
		break;
	}
	case SEEK_CUR: {
		if (offset > 0) {
			// Cek overflow jika offset positif
			if (curr_fd->pos >
			    (uint64_t)__INT64_MAX__ - (uint64_t)offset) {
				return -EINVAL;
			}
			new_pos = (long)curr_fd->pos + offset;
		} else if (offset < 0) {
			// Cek underflow jika offset negatif
			uint64_t abs_offset = (uint64_t)(-offset);
			if (abs_offset > curr_fd->pos) {
				return -EINVAL;
			}
			new_pos = (long)(curr_fd->pos - abs_offset);
		} else {
			new_pos = (long)curr_fd->pos;
		}
		break;
	}
	case SEEK_END: {
		long size = (long)curr_fd->vnode->size;
		if (offset > 0) {
			if (size > __INT64_MAX__ - offset) {
				return -EINVAL;
			}
			new_pos = size + offset;
		} else if (offset < 0) {
			if (-offset > size) {
				return -EINVAL;
			}
			new_pos = size + offset;
		} else {
			new_pos = size;
		}
		break;
	}
	default:
		return -EINVAL;
	}

	curr_fd->pos = (uint64_t)new_pos;
	return new_pos;
}