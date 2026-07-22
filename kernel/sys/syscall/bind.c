#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include "vfs/dentry.h"
#include "vfs/vnode.h"
#include <net/socket.h>
#include <sys/fd.h>
#include <sys/syscall.h>
#include <vfs/enum.h>

int syscall_bind(int fd, const void* addr, uint32_t len) {
	if (!addr || !len) {
		return -EINVAL;
	}

	auto curr_thread = get_current_core_data()->active_thread;
	if (!curr_thread) {
		return -ENOENT;
	}

	auto proc = curr_thread->process;
	if (!proc) {
		return -ENOENT;
	}
	auto fdtable = proc->fdtable;
	if (!fdtable) {
		return -ENOENT;
	}

	if (fd < 0 || (uint32_t)fd >= fdtable->max_fds) {
		return -EBADF;
	}

	auto fd_ = fdtable->fds[fd];
	if (!fd_) {
		return -EBADF;
	}

	auto socket = (socket_t*)fd_->private_data;
	if (!socket) {
		return -ENOENT;
	}

	serial2_printf("bind a socket with family %d\n", socket->family);
	switch (socket->family) {
	case AF_UNIX: {
		struct sockaddr_un* addr_ = (struct sockaddr_un*)addr;
		auto path = addr_->sun_path;
		serial2_printf("path : %s\n", path);
		if (!path) {
			return -EINVAL;
		}

		dentry_ptr out;
		int vxr = vxnamei(path, &out);
		serial2_printf("bind vxnamei returned %d\n", vxr);
		/* vxnamei creates dummy dentries for missing paths.
		 * Only fail if a REAL file (with fs_instance) exists. */
		if (vxr == VFS_OK && out && out->vnode &&
		    out->vnode->fs_instance) {
			serial2_printf("bind EADDRINUSE path exists\n");
			return -EADDRINUSE;
		}

		// TODO: create socket file at path
		// Attach socket to vnode so connect() can find it
		out->vnode->vnode_private = socket;
		socket->state = SOCK_STATE_BOUND;
		serial2_printf("bind AF_UNIX ok\n");
		break;
	}
	default:
		return -ENOSYS;
	}

	return 0;
}
