#include "hal/cpu/core.h"
#include "str.h"
#include "memory/kalloc.h"
#include "procc/process.h"
#include "sys/err_no.h"
#include "sys/fd.h"
#include "vfs/vnode.h"
#include "vfs/vfs.h"
#include <sys/syscall.h>
#include "libk/serial.h"

int syscall_pipe2(int pipefd[2], int flags);
int syscall_pipe(int pipefd[2]);

#define PIPE_MAX_RING_BUFFER 4096

struct pipe_ring {
	uint8_t buf[PIPE_MAX_RING_BUFFER];
	size_t head;
	size_t tail;
};

struct internal_pipe {
	struct pipe_ring ring;
	struct thread* reader_waiter;
	struct thread* writer_waiter;
	atomic_t reader_count;
	atomic_t writer_count;
};

static long pipe_ring_write(struct pipe_ring* ring, const void* buf, size_t len) {
	const size_t mask = PIPE_MAX_RING_BUFFER - 1;
	size_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);
	size_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
	size_t avail = PIPE_MAX_RING_BUFFER - (head - tail);

	if (len > avail)
		len = avail;
	if (len == 0)
		return 0;

	size_t offset = head & mask;
	size_t first_part = PIPE_MAX_RING_BUFFER - offset;
	if (first_part > len)
		first_part = len;

	const uint8_t* cbuf = (const uint8_t*)buf;
	memcopy(&ring->buf[offset], (void*)cbuf, first_part);
	if (len > first_part)
		memcopy(&ring->buf[0], (void*)(cbuf + first_part), len - first_part);

	__atomic_store_n(&ring->head, head + len, __ATOMIC_RELEASE);
	return (long)len;
}

static int pipe_ring_read(struct pipe_ring* ring, void* buf, size_t len) {
	const size_t mask = PIPE_MAX_RING_BUFFER - 1;
	size_t head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
	size_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
	size_t avail = head - tail;

	if (len > avail)
		len = avail;
	if (len == 0)
		return 0;

	size_t offset = tail & mask;
	size_t first_part = PIPE_MAX_RING_BUFFER - offset;
	if (first_part > len)
		first_part = len;

	uint8_t* cbuf = (uint8_t*)buf;
	memcopy(cbuf, &ring->buf[offset], first_part);
	if (len > first_part)
		memcopy(cbuf + first_part, &ring->buf[0], len - first_part);

	__atomic_store_n(&ring->tail, tail + len, __ATOMIC_RELEASE);
	return (int)len;
}

static int pipe_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	(void)offset;
	auto fd = (struct file_descriptor*)vnode->vnode_private;
	if (!fd) return -EINVAL;
	auto internal = (struct internal_pipe*)fd->private_data;
	if (!internal) return -EINVAL;

	while (1) {
		int ret = pipe_ring_read(&internal->ring, buf, len);
		if (ret > 0) {
			if (internal->writer_waiter) {
				vxThreadWake(internal->writer_waiter);
				internal->writer_waiter = NULL;
			}
			return ret;
		}
		if (len == 0) return 0;
		if (__atomic_load_n(&internal->writer_count.counter, __ATOMIC_RELAXED) == 0) {
			serial2_printf("pipe_read: writer_count is 0, returning EOF\n");
			return 0; // EOF
		}

		if (fd->flags & 04000) // O_NONBLOCK
			return -EAGAIN;

		serial2_printf("pipe_read: blocking thread %d, writer_count=%d\n", 
			get_current_core_data()->active_thread->id,
			__atomic_load_n(&internal->writer_count.counter, __ATOMIC_RELAXED));
		internal->reader_waiter = get_current_core_data()->active_thread;
		thread_block();
	}
}

static long pipe_write(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	(void)offset;
	auto fd = (struct file_descriptor*)vnode->vnode_private;
	if (!fd) return -EINVAL;
	auto internal = (struct internal_pipe*)fd->private_data;
	if (!internal) return -EINVAL;

	if (__atomic_load_n(&internal->reader_count.counter, __ATOMIC_RELAXED) == 0) return -EPIPE;

	size_t written = 0;
	while (written < len) {
		long ret = pipe_ring_write(&internal->ring, (uint8_t*)buf + written, len - written);
		if (ret > 0) {
			written += (size_t)ret;
			if (internal->reader_waiter) {
				vxThreadWake(internal->reader_waiter);
				internal->reader_waiter = NULL;
			}
		} else {
			if (__atomic_load_n(&internal->reader_count.counter, __ATOMIC_RELAXED) == 0) return written > 0 ? (long)written : -EPIPE;
			
			if (fd->flags & 04000) { // O_NONBLOCK
				if (written > 0) return (long)written;
				return -EAGAIN;
			}

			internal->writer_waiter = get_current_core_data()->active_thread;
			thread_block();
		}
	}
	return (long)written;
}

extern vops_file_t pipe_read_ops;
extern vops_file_t pipe_write_ops;

void pipe_close_fd(struct file_descriptor* fd);
void pipe_dup_fd(struct file_descriptor* fd);

void pipe_close_fd(struct file_descriptor* fd) {
	if (!fd) return;
	auto internal = (struct internal_pipe*)fd->private_data;
	if (!internal) return;
	
	serial2_printf("pipe_close_fd called on %s\n", fd->ops == &pipe_read_ops ? "read" : "write");

	if (fd->ops == &pipe_read_ops) {
		int r = __atomic_sub_fetch(&internal->reader_count.counter, 1, __ATOMIC_SEQ_CST);
		serial2_printf("pipe_close_fd: reader_count becomes %d\n", r);
		if (r == 0) {
			if (internal->writer_waiter) {
				serial2_printf("pipe_close_fd: waking writer_waiter\n");
				vxThreadWake(internal->writer_waiter);
				internal->writer_waiter = NULL;
			}
		}
	} else if (fd->ops == &pipe_write_ops) {
		int w = __atomic_sub_fetch(&internal->writer_count.counter, 1, __ATOMIC_SEQ_CST);
		serial2_printf("pipe_close_fd: writer_count becomes %d\n", w);
		if (w == 0) {
			if (internal->reader_waiter) {
				serial2_printf("pipe_close_fd: waking reader_waiter\n");
				vxThreadWake(internal->reader_waiter);
				internal->reader_waiter = NULL;
			}
		}
	}
	
	if (__atomic_load_n(&internal->reader_count.counter, __ATOMIC_RELAXED) == 0 &&
	    __atomic_load_n(&internal->writer_count.counter, __ATOMIC_RELAXED) == 0) {
		kfree2(internal);
		fd->private_data = NULL;
	}
}

void pipe_dup_fd(struct file_descriptor* fd) {
	if (!fd) return;
	auto internal = (struct internal_pipe*)fd->private_data;
	if (!internal) return;
	
	if (fd->ops == &pipe_read_ops) {
		int r = __atomic_add_fetch(&internal->reader_count.counter, 1, __ATOMIC_SEQ_CST);
		serial2_printf("pipe_dup_fd: reader_count becomes %d\n", r);
	} else if (fd->ops == &pipe_write_ops) {
		int w = __atomic_add_fetch(&internal->writer_count.counter, 1, __ATOMIC_SEQ_CST);
		serial2_printf("pipe_dup_fd: writer_count becomes %d\n", w);
	}
}

vops_file_t pipe_read_ops = {
	.read = pipe_read,
};

vops_file_t pipe_write_ops = {
	.write = pipe_write,
};

int syscall_pipe2(int pipefd[2], int flags) {
	auto curr_procc = get_current_core_data()->active_thread->process;
	auto fdt = (struct fdtable*)curr_procc->fdtable;

	int rfd = -1, wfd = -1;
	for (int i = 0; i < (int)fdt->max_fds; i++) {
		if (!fdt->fds[i]) {
			if (rfd == -1) rfd = i;
			else if (wfd == -1) { wfd = i; break; }
		}
	}

	if (rfd == -1 || wfd == -1) return -EMFILE;

	auto internal = (struct internal_pipe*)kalloc(sizeof(struct internal_pipe));
	memset(internal, 0, sizeof(*internal));
	internal->reader_count.counter = 1;
	internal->writer_count.counter = 1;

	auto rfile = alloc_fd();
	rfile->ops = &pipe_read_ops;
	rfile->private_data = internal;
	if (flags & 04000) rfile->flags |= 04000; // O_NONBLOCK
	fdt->fds[rfd] = rfile;
	if (flags & 02000000) fdt->fd_flags[rfd] = 1; // O_CLOEXEC
	else fdt->fd_flags[rfd] = 0;

	auto wfile = alloc_fd();
	wfile->ops = &pipe_write_ops;
	wfile->private_data = internal;
	if (flags & 04000) wfile->flags |= 04000; // O_NONBLOCK
	fdt->fds[wfd] = wfile;
	if (flags & 02000000) fdt->fd_flags[wfd] = 1; // O_CLOEXEC
	else fdt->fd_flags[wfd] = 0;

	auto rvnode = create_and_attach_vnode();
	rvnode->type = VNODE_TYPE_FIFO;
	rvnode->ops = &pipe_read_ops;
	rvnode->vnode_private = rfile;
	rfile->vnode = rvnode;

	auto wvnode = create_and_attach_vnode();
	wvnode->type = VNODE_TYPE_FIFO;
	wvnode->ops = &pipe_write_ops;
	wvnode->vnode_private = wfile;
	wfile->vnode = wvnode;

	fdt->fds[rfd] = rfile;
	fdt->fds[wfd] = wfile;

	if (rfd >= (int)fdt->next_fd) fdt->next_fd = (uint32_t)rfd + 1;
	if (wfd >= (int)fdt->next_fd) fdt->next_fd = (uint32_t)wfd + 1;

	pipefd[0] = rfd;
	pipefd[1] = wfd;

	return 0;
}

int syscall_pipe(int pipefd[2]) {
	return syscall_pipe2(pipefd, 0);
}
