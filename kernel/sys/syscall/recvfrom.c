#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/scheduler.h"
#include "sys/err_no.h"
#include <net/socket.h>
#include <sys/fd.h>
#include <sys/syscall.h>

#define debug 0

/* must match musl: O_NONBLOCK = 04000 */
#define O_NONBLOCK 04000

int syscall_recvfrom(int fd, void* buf, uint32_t len, int flags, void* src_addr, uint32_t* addrlen) {
	(void)flags;
	(void)src_addr;
	(void)addrlen;

	if (fd < 0 || !buf || !len)
		return -EINVAL;

	auto curr_thread = get_current_core_data()->active_thread;
	if (!curr_thread)
		return -ENOENT;
	auto proc = curr_thread->process;
	if (!proc)
		return -ENOENT;
	auto fdtable = proc->fdtable;
	if (!fdtable)
		return -ENOENT;
	if ((uint32_t)fd >= fdtable->max_fds)
		return -EBADF;

	auto fd_ = fdtable->fds[fd];
	if (!fd_)
		return -EBADF;

	auto socket = (socket_t*)fd_->private_data;
	if (!socket)
		return -ENOENT;

	if (socket->family != AF_UNIX)
		return -EAFNOSUPPORT;

	struct unix_socket* us = (struct unix_socket*)socket;

	int is_nonblock = (fd_->flags & O_NONBLOCK) != 0;
	// serial2_printf("recvfrom fd=%d is_nonblock=%d flags=0x%x rcount=%d\n", fd, is_nonblock, fd_->flags, us->rcount);

	/* Wait loop — block if nonblock not set */
	while (us->rcount == 0) {
		if (is_nonblock) {
			return -EAGAIN;
		}
		/* Block until sendto() wakes us */
		us->blocked_recv_thread = curr_thread;
		thread_block();
		us->blocked_recv_thread = NULL;
	}

	/* Read from ring buffer */
	int nread = 0;
	while (nread < (int)len && us->rcount > 0) {
		((char*)buf)[nread] = us->rbuf[us->rhead];
		us->rhead = (us->rhead + 1) % UNIX_BUF_SIZE;
		us->rcount--;
		nread++;
	}

	// serial2_printf("recvfrom fd=%d: read %d bytes (buf %d/%d)\n",
	//                fd, nread, us->rcount, UNIX_BUF_SIZE);
	return nread;
}
