#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include "vfs/vnode.h"
#include <sys/fd.h>
#include <sys/syscall.h>

int syscall_fsync(int fd) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;

	if (fd < 0 || fd >= (int)fdt->max_fds) {
		LOG2_ERROR("fsync", "fd %d is invalid, max fd %d", fd, fdt->max_fds);
		return -EBADF;
	}

	auto curr_fd = fdt->fds[fd];
	if (!curr_fd || !curr_fd->vnode) {
		LOG2_ERROR("fsync", "fd %d vnode is missing", fd);
		return -EBADF;
	}

	// if (curr_fd->write_buffer && curr_fd->write_buffer_size > 0) {
	// 	auto ops = (vops_file_t*)curr_fd->ops;
	// 	if (ops && ops->write) {
	// 		ops->write(curr_fd->vnode, curr_fd->write_buffer, curr_fd->write_buffer_size, curr_fd->pos - curr_fd->write_buffer_size);
	// 	}
	// 	curr_fd->write_buffer_size = 0;
	// }
	if (curr_fd->vnode->type == VNODE_TYPE_FILE) {
		auto ops = (vops_file_t*)curr_fd->ops;
		if (ops && ops->flush) {
			ops->flush(curr_fd->vnode);
		}
	} else if (curr_fd->vnode->type == VNODE_TYPE_BLK) {
		auto ops = (vops_blk_t*)curr_fd->ops;
		if (ops && ops->flush) {
			ops->flush(curr_fd->vnode);
		}
	}

	return 0;
}
