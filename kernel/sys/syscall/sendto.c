#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/scheduler.h"
#include "sys/err_no.h"
#include "vfs/dentry.h"
#include <net/socket.h>
#include <sys/fd.h>
#include <sys/syscall.h>

#define O_NONBLOCK 04000

#define SENDTO_DEBUG 0

int syscall_sendto(int fd, const void* buf, uint32_t len, int flags, const void* dest_addr, uint32_t addrlen) {
	(void)flags;
	(void)dest_addr;
	(void)addrlen;

	serial2_printf("sendto fd=%d len=%u\n", fd, len);

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

	/* For connected sockets, send to peer's buffer */
	struct unix_socket* dest = us->peer ? us->peer : us;

#if SENDTO_DEBUG
	serial2_printf("sendto fd=%d len=%u us=%p peer=%p dest=%p\n", fd, len, us, us->peer, dest);
#endif

	int is_nonblock = (fd_->flags & O_NONBLOCK) != 0;

	if (dest->rcount >= UNIX_BUF_SIZE) {
		if (is_nonblock) {
			return -EAGAIN;
		}

		while (dest->rcount >= UNIX_BUF_SIZE) {
			thread_block();
		}
	}

	int written = 0;
	while (written < (int)len && dest->rcount < UNIX_BUF_SIZE) {
		dest->rbuf[dest->rtail] = ((const char*)buf)[written];
		dest->rtail = (dest->rtail + 1) % UNIX_BUF_SIZE;
		dest->rcount++;
		written++;
	}

	if (dest->blocked_recv_thread) {
		vxThreadWake(dest->blocked_recv_thread);
	}

#if SENDTO_DEBUG
	serial2_printf("sendto from thread_id %d wrote %d bytes (buf %d/%d)\n", curr_thread->id, written, dest->rcount, UNIX_BUF_SIZE);
#endif

	// print_dentry_tree(get_root_dentry(), 0);
	return written;
}
