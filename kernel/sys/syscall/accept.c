#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/scheduler.h"
#include "str.h"
#include "sys/err_no.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vnode.h"
#include <net/socket.h>
#include <sys/fd.h>
#include <sys/syscall.h>

/* must match musl: O_NONBLOCK = 04000 */
#define O_NONBLOCK 04000

int syscall_accept(int fd, void* addr, uint32_t* addrlen) {
	(void)addr;
	(void)addrlen;

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

	auto server = (socket_t*)fd_->private_data;
	if (!server)
		return -ENOENT;

	/* Must be a listening UNIX socket */
	if (server->family != AF_UNIX)
		return -EAFNOSUPPORT;

	if (server->state != SOCK_STATE_LISTENING) {
		serial2_printf("accept: not listening (state=%d)\n",
		               server->state);
		return -EINVAL;
	}

	struct unix_socket* us = (struct unix_socket*)server;

	int is_nonblock = (fd_->flags & O_NONBLOCK) != 0;

	/* Wait loop — block if nonblock not set */
	while (us->pending_count == 0) {
		if (is_nonblock) {
			return -EAGAIN;
		}
		/* Block until connect() wakes us */
		us->blocked_accept_thread = curr_thread;
		thread_block();
		us->blocked_accept_thread = NULL;
	}

	/* Dequeue client (which is actually the server_side created in connect) */
	socket_t* client = us->pending[us->pending_head];
	us->pending_head = (us->pending_head + 1) % UNIX_BACKLOG_MAX;
	us->pending_count--;

	serial2_printf("accept: dequeued client=%p (pending=%d/%d)\n",
	               client, us->pending_count, server->backlog);

	/* Allocate new fd for the accepted connection */
	uint32_t client_fd = 0;
	uint32_t max = fdtable->max_fds;
	bool found = false;
	for (uint32_t i = 0; i < max; i++) {
		client_fd = (fdtable->next_fd + i) % max;
		if (fdtable->fds[client_fd] == nullptr) {
			found = true;
			break;
		}
	}
	if (!found)
		return -EMFILE;

	fdtable->next_fd = (client_fd + 1) % max;

	auto new_fd = alloc_fd();
	new_fd->fdt = fdtable;
	fdtable->fds[client_fd] = new_fd;
	new_fd->private_data = client;

	/* Create /proc/<pid>/fd/<client_fd> dentry entry */
	{
		dentry_ptr proc_dentry;
		if (resolve_dentry("/proc", 0, &proc_dentry, 0) == VFS_OK) {
			dentry_ptr curr_proc_dentry;
			if (resolve_dentry(itoa(proc->pid, 10), proc_dentry,
			                   &curr_proc_dentry,
			                   CREATE_MISSING_ENTRY) == VFS_OK) {
				if (curr_proc_dentry->vnode) {
					curr_proc_dentry->vnode->type = VNODE_TYPE_DIR;
					curr_proc_dentry->vnode->permission = 0555;
				}
				dentry_ptr fd_dentry;
				if (resolve_dentry(itoa(client_fd, 10), curr_proc_dentry,
				                   &fd_dentry,
				                   CREATE_MISSING_ENTRY) == VFS_OK) {
					dentry_put(fd_dentry);
				}
				dentry_put(curr_proc_dentry);
			}
			dentry_put(proc_dentry);
		}
	}

	/* Mark client as connected */
	client->state = SOCK_STATE_CONNECTED;

	serial2_printf("accept: new fd=%d for client family=%d\n",
	               client_fd, client->family);

	return (int)client_fd;
}
