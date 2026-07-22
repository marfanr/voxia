#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include <net/socket.h>
#include <sys/fd.h>
#include <sys/syscall.h>

#define SOMAXCONN 128

int syscall_listen(int fd, int backlog) {
	serial2_printf("syscall_listen entered fd=%d backlog=%d\n", fd, backlog);

	if (fd < 0) {
		serial2_printf("syscall_listen: bad fd\n");
		return -EBADF;
	}

	auto curr_thread = get_current_core_data()->active_thread;
	if (!curr_thread) {
		serial2_printf("syscall_listen: no thread\n");
		return -ENOENT;
	}

	auto proc = curr_thread->process;
	if (!proc) {
		serial2_printf("syscall_listen: no proc\n");
		return -ENOENT;
	}
	auto fdtable = proc->fdtable;
	if (!fdtable) {
		serial2_printf("syscall_listen: no fdtable\n");
		return -ENOENT;
	}

	if ((uint32_t)fd >= fdtable->max_fds) {
		serial2_printf("syscall_listen: fd out of range\n");
		return -EBADF;
	}

	auto fd_ = fdtable->fds[fd];
	if (!fd_) {
		serial2_printf("syscall_listen: no fd entry\n");
		return -EBADF;
	}

	auto socket = (socket_t*)fd_->private_data;
	if (!socket) {
		serial2_printf("syscall_listen: fd has no socket\n");
		return -ENOENT;
	}

	/* socket must be bound or already listening */
	if (socket->state != SOCK_STATE_BOUND &&
	    socket->state != SOCK_STATE_LISTENING) {
		serial2_printf("syscall_listen: socket not bound (state=%d)\n",
		               socket->state);
		return -EINVAL;
	}

	/* socket type must be connection-oriented */
	if (socket->type != SOCK_STREAM && socket->type != SOCK_SEQPACKET) {
		serial2_printf("syscall_listen: wrong socket type %d\n",
		               socket->type);
		return -EINVAL;
	}

	/* clamp backlog */
	if (backlog < 0)
		backlog = 0;
	if (backlog > SOMAXCONN)
		backlog = SOMAXCONN;

	socket->state = SOCK_STATE_LISTENING;
	socket->backlog = backlog;

	serial2_printf("listen on socket fd=%d family=%d backlog=%d state=%d\n",
	               fd, socket->family, backlog, socket->state);

	return 0;
}
