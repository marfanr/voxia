#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "net/socket.h"
#include "str.h"
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
	if (!curr_fd) {
		LOG2_ERROR("read", "fd %d is missing", fd);
		return -EBADF;
	}

	/* Route socket reads to recvfrom */
	if (!curr_fd->vnode && curr_fd->private_data) {
		auto sock = (socket_t*)curr_fd->private_data;
		if (sock->family == AF_UNIX || sock->family == AF_INET) {
			return syscall_recvfrom(fd, buf, (uint32_t)count, 0, NULL, 0);
		}
	}

	if (!curr_fd->vnode) {
		LOG2_ERROR("read", "fd %d vnode is missing", fd);
		return -EBADF;
	}

	auto ops = (vops_file_t*)curr_fd->ops;
	if (!ops) {
		LOG2_ERROR("read", "fd %d ops is missing on process %d", fd, curr_procc->pid);
		return -EBADF;
	}

	if (curr_fd->vnode->type == VNODE_TYPE_DIR) {
		return -EISDIR;
	}

	if (!ops->read) {
		LOG2_ERROR("read", "fd %d `read` ops is missing on process %d", fd, curr_procc->pid);
		return -EINVAL;
	}

	void* kbuf = kalloc((size_t)count);
	if (!kbuf) return -ENOMEM;

	int read_count = ops->read(curr_fd->vnode, kbuf, (size_t)count, curr_fd->pos);
	
	if (read_count > 0) {
		paging_reload(curr_procc->page);
		memcopy(buf, kbuf, (size_t)read_count);
		curr_fd->pos += (size_t)read_count;
	}
	
	kfree2(kbuf);
	return read_count;
}