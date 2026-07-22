#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "procc/scheduler.h"
#include "sys/err_no.h"
#include "vfs/dentry.h"
#include "vfs/vnode.h"
#include <net/socket.h>
#include <string.h>
#include <sys/fd.h>
#include <sys/syscall.h>
#include <vfs/enum.h>

int syscall_connect(int fd, const void* addr, uint32_t len) {
	if (!addr || !len) {
		return -EINVAL;
	}

	auto curr_thread = get_current_core_data()->active_thread;
	if (!curr_thread)
		return -ENOENT;

	auto proc = curr_thread->process;
	if (!proc)
		return -ENOENT;
	auto fdtable = proc->fdtable;
	if (!fdtable)
		return -ENOENT;

	if (fd < 0 || (uint32_t)fd >= fdtable->max_fds)
		return -EBADF;

	auto fd_ = fdtable->fds[fd];
	if (!fd_)
		return -EBADF;

	auto client = (socket_t*)fd_->private_data;
	if (!client)
		return -ENOENT;

	/* Only AF_UNIX connect supported for now */
	if (client->family != AF_UNIX)
		return -EAFNOSUPPORT;

	struct sockaddr_un* addr_ = (struct sockaddr_un*)addr;
	auto path = addr_->sun_path;
	if (!path)
		return -EINVAL;

	serial2_printf("connect to path %s\n", path);

	/* Look up server socket via vxnamei → vnode → vnode_private */
	dentry_ptr out;
	if (vxnamei(path, &out) != VFS_OK || !out || !out->vnode) {
		serial2_printf("connect: path not found\n");
		return -ENOENT;
	}

	auto server = (socket_t*)out->vnode->vnode_private;
	if (!server) {
		serial2_printf("connect: no socket at path\n");
		return -ECONNREFUSED;
	}

	if (server->state != SOCK_STATE_LISTENING) {
		serial2_printf("connect: server not listening (state=%d)\n",
		               server->state);
		return -ECONNREFUSED;
	}

	struct unix_socket* us = (struct unix_socket*)server;

	/* Check backlog */
	if (us->pending_count >= server->backlog) {
		serial2_printf("connect: backlog full (%d/%d)\n",
		               us->pending_count, server->backlog);
		return -ECONNREFUSED;
	}

	/* Create server-side socket with its own rbuf, linked via peer */
	struct unix_socket* client_us = (struct unix_socket*)client;
	struct unix_socket* server_side = kalloc(sizeof(struct unix_socket));
	if (!server_side)
		return -ENOMEM;
	memset(server_side, 0, sizeof(struct unix_socket));
	memcopy(&server_side->base, client, sizeof(socket_t));
	server_side->peer = client_us;
	client_us->peer = server_side;

	/* Queue server_side (not client) in pending[] */
	us->pending[us->pending_tail] = (socket_t*)server_side;
	us->pending_tail = (us->pending_tail + 1) % UNIX_BACKLOG_MAX;
	us->pending_count++;

	serial2_printf("connect: queued server_side=%p peer=%p (pending=%d/%d)\n",
	               server_side, client_us, us->pending_count, server->backlog);

	/* Wake up server thread blocked in accept() */
	if (us->blocked_accept_thread) {
		vxThreadWake(us->blocked_accept_thread);
	}

	return 0;
}
