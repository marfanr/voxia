#include "pty.h"
#include "hal/cpu/core.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "procc/process.h"
#include "procc/thread.h"
#include "str.h"
#include "string.h"
#include "sys/err_no.h"
#include "sys/fd.h"
#include "sys/sig.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <vfs/ioctl.h>

static dentry_ptr ptmx_dentry;
static uint32_t slave_id = 0;

/* ═══════════════════════════════════════════════════════════════════
 * MASTER OPERATIONS
 * ═══════════════════════════════════════════════════════════════════ */
static int pty_master_ioctl(vnode_ptr_t vnode, uint32_t req, void* arg) {
	auto fd = (struct file_descriptor*)vnode->vnode_private;
	if (!fd)
		return -VFS_ENOENT;

	auto internal = (struct internal_pty*)fd->private_data;
	if (!internal)
		return -VFS_ENOENT;

	serial2_printf("ioctl on pty master with req : 0x%x\n", req);

	switch (req) {
	case TIOCSPTLCK: {
		internal->locked = 0;
		return 0;
	}
	case TIOCGPTN: {
		*(uint32_t*)arg = internal->id;
		return 0;
	}
	}

	return -ENOTTY;
}

static long pty_ring_write(struct pty_ring* ring, const void* buf, size_t len) {
	const size_t mask = PTY_MAX_RING_BUFFER - 1;
	size_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);
	size_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
	size_t avail = PTY_MAX_RING_BUFFER - (head - tail);

	if (len > avail)
		len = avail;
	if (len == 0)
		return 0;

	size_t offset = head & mask;
	size_t first_part = PTY_MAX_RING_BUFFER - offset;
	if (first_part > len)
		first_part = len;

	memcopy(ring->buf + offset, (void*)buf, first_part);
	if (len > first_part) {
		memcopy(ring->buf, (void*)((const uint8_t*)buf + first_part), len - first_part);
	}

	__atomic_store_n(&ring->head, head + len, __ATOMIC_RELEASE);
	return (long)len;
}

static int pty_ring_read(struct pty_ring* ring, void* buf, size_t len) {
	const size_t mask = PTY_MAX_RING_BUFFER - 1;
	size_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
	size_t head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
	size_t avail = head - tail;

	if (len > avail)
		len = avail;
	if (len == 0)
		return 0;

	size_t offset = tail & mask;
	size_t first_part = PTY_MAX_RING_BUFFER - offset;
	if (first_part > len)
		first_part = len;

	memcopy(buf, ring->buf + offset, first_part);
	if (len > first_part) {
		memcopy((uint8_t*)buf + first_part, ring->buf, len - first_part);
	}

	__atomic_store_n(&ring->tail, tail + len, __ATOMIC_RELEASE);
	return (int)len;
}

static long pty_master_write(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	(void)offset;
	auto fd = (struct file_descriptor*)vnode->vnode_private;
	if (!fd)
		return -EINVAL;
	auto internal = (struct internal_pty*)fd->private_data;
	if (!internal || internal->slave_closed)
		return -EPIPE;

	long ret = pty_ring_write(&internal->master_to_slave, buf, len);
	if (internal->slave_waiter) {
		vxThreadWake(internal->slave_waiter);
		internal->slave_waiter = NULL;
	}

	/* Local echo: Hanya echo jika ICANON & ECHO aktif (tergantung implementasi line discipline) */
	if (internal->termios.c_lflag & 0000010) {
		pty_ring_write(&internal->slave_to_master, buf, len);
		if (internal->master_waiter) {
			vxThreadWake(internal->master_waiter);
			internal->master_waiter = NULL;
		}
	}

	return ret;
}

static int pty_master_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	(void)offset;
	auto fd = (struct file_descriptor*)vnode->vnode_private;
	if (!fd)
		return -EINVAL;
	auto internal = (struct internal_pty*)fd->private_data;
	if (!internal)
		return -EINVAL;

	while (1) {
		int ret = pty_ring_read(&internal->slave_to_master, buf, len);
		if (ret > 0 || len == 0)
			return ret;

		/* EOF jika slave sudah di-close */
		if (internal->slave_closed)
			return 0;

		/* Cek flag O_NONBLOCK (04000) */
		if (fd->flags & 04000)
			return -EAGAIN;

		internal->master_waiter = get_current_core_data()->active_thread;
		thread_block();
	}
}

static int pty_master_poll(vnode_t* vnode, thread_t* waiter) {
	auto fd = (struct file_descriptor*)vnode->vnode_private;
	if (!fd)
		return 0;
	auto internal = (struct internal_pty*)fd->private_data;
	if (!internal)
		return 0;

	size_t tail = __atomic_load_n(&internal->slave_to_master.tail, __ATOMIC_RELAXED);
	size_t head = __atomic_load_n(&internal->slave_to_master.head, __ATOMIC_ACQUIRE);

	if ((head - tail) == 0 && waiter)
		internal->master_waiter = waiter;
	return (head - tail) > 0 || internal->slave_closed;
}

/* Fungsi close opsional (jika VFS kamu mendukung pointer fungsi close/release di vops_file_t) */
__attribute__((unused)) static int pty_master_close(vnode_t* vnode) {
	auto fd = (struct file_descriptor*)vnode->vnode_private;
	if (!fd)
		return -EINVAL;
	auto internal = (struct internal_pty*)fd->private_data;
	if (internal) {
		internal->master_closed = 1;
		/* Wakeup slave agar tidak hang saat membaca/menulis */
		if (internal->slave_waiter) {
			vxThreadWake(internal->slave_waiter);
			internal->slave_waiter = NULL;
		}
	}
	return 0;
}

vops_file_t pty_master_ops = {
    .ioctl = pty_master_ioctl, .write = pty_master_write, .read = pty_master_read, .poll = pty_master_poll,
    /* .close = pty_master_close,  <-- Uncomment jika struct vops_file_t memiliki fungsi close/release */
};

/* ═══════════════════════════════════════════════════════════════════
 * SLAVE OPERATIONS
 * ═══════════════════════════════════════════════════════════════════ */
static int pty_slave_ioctl(vnode_ptr_t vnode, uint32_t req, void* arg) {
	auto internal = (struct internal_pty*)vnode->vnode_private;
	if (!internal)
		return -VFS_ENOENT;

	serial2_printf("ioctl on pty slave with req : 0x%x\n", req);

	if (!arg && req != TIOCSCTTY)
		return -EINVAL;

	switch (req) {
	case TCGETS: {
		memcopy(arg, &internal->termios, sizeof(struct termios));
		return 0;
	}
	case TCSETS:
	case 0x5403: /* TCSETSW */
	case 0x5404: /* TCSETSF */ {
		memcopy(&internal->termios, arg, sizeof(struct termios));
		return 0;
	}
	case TIOCGWINSZ: {
		memcopy(arg, &internal->ws, sizeof(struct win_size));
		return 0;
	}
	case TIOCSWINSZ: {
		memcopy(&internal->ws, arg, sizeof(struct win_size));
        auto fg = internal->foreground;
        if (fg) {
            auto proc = find_process_by_pid(fg);
            if (proc) {
                sig_send(proc->signal, SIGWINCH);
            }
        } 
        
		return 0;
	}
	case TIOCGPGRP: {
		if (internal->foreground) {
			*(pid_t*)arg = internal->foreground;
		} else {
			auto curr_procc = get_current_core_data()->active_thread->process;
			*(pid_t*)arg = curr_procc->pgid ? curr_procc->pgid : curr_procc->pid;
		}
		return 0;
	}
	case TIOCSPGRP: {
		internal->foreground = *(pid_t*)arg;
		return 0;
	}
	case TIOCSCTTY: {
		return 0;
	}
	}

	return -ENOTTY;
}

static long pty_slave_write(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	(void)offset;

	auto internal = (struct internal_pty*)vnode->vnode_private;
	if (!internal || internal->master_closed)
		return -EPIPE;

	long ret = pty_ring_write(&internal->slave_to_master, buf, len);
	if (internal->master_waiter) {
		vxThreadWake(internal->master_waiter);
		internal->master_waiter = NULL;
	}
	return ret;
}

static int pty_slave_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	(void)offset;

	auto internal = (struct internal_pty*)vnode->vnode_private;
	if (!internal)
		return -EINVAL;

	while (1) {
		int ret = pty_ring_read(&internal->master_to_slave, buf, len);
		if (ret > 0 || len == 0)
			return ret;

		if (internal->master_closed)
			return 0;

		internal->slave_waiter = get_current_core_data()->active_thread;
		thread_block();
	}
}

static int pty_slave_poll(vnode_t* vnode, thread_t* waiter) {
	auto internal = (struct internal_pty*)vnode->vnode_private;
	if (!internal)
		return 0;

	size_t tail = __atomic_load_n(&internal->master_to_slave.tail, __ATOMIC_RELAXED);
	size_t head = __atomic_load_n(&internal->master_to_slave.head, __ATOMIC_ACQUIRE);

	if ((head - tail) == 0 && waiter)
		internal->slave_waiter = waiter;
	return (head - tail) > 0 || internal->master_closed;
}

__attribute__((unused)) static int pty_slave_close(vnode_t* vnode) {
	auto internal = (struct internal_pty*)vnode->vnode_private;
	if (internal) {
		internal->slave_closed = 1;
		if (internal->master_waiter) {
			vxThreadWake(internal->master_waiter);
			internal->master_waiter = NULL;
		}
	}
	return 0;
}

vops_file_t pty_slave_ops = {
    .ioctl = pty_slave_ioctl, .write = pty_slave_write, .read = pty_slave_read, .poll = pty_slave_poll,
    // .close = pty_slave_close
};

/* ═══════════════════════════════════════════════════════════════════
 * /dev/ptmx ENTRY POINT
 * ═══════════════════════════════════════════════════════════════════ */
static void ptmx_open(vnode_ptr_t vnode, void* fd) {
	struct file_descriptor* f = (struct file_descriptor*)fd;

	serial2_printf("opened pts : %d\n", slave_id);

	auto pts_path = str("/dev/pts/");
	auto curr_pts = str_concat(pts_path, itoa(slave_id, 10));
	dentry_ptr slave_dentry;
	if (resolve_dentry(curr_pts->c_str, 0, &slave_dentry, CREATE_MISSING_ENTRY) != VFS_OK) {
		str_release(pts_path);
		str_release(curr_pts);
		return;
	}
	str_release(pts_path);
	str_release(curr_pts);

	vnode_ptr_t slave_vnode = create_and_attach_vnode();
	slave_vnode->type = VNODE_TYPE_CHR;
	slave_dentry->vnode = slave_vnode;
	slave_vnode->ops = &pty_slave_ops;

	f->ops = &pty_master_ops;
	f->private_data = kalloc(sizeof(struct internal_pty));

	/* Perhatian: VFS desain saat ini menggunakan fd untuk master, tetapi
	   mengirim f->private_data (internal_pty) langsung untuk slave.
	   Biarkan saja dulu agar tidak merusak logika VFS yang ada. */
	slave_vnode->vnode_private = f->private_data;

	auto internal = (struct internal_pty*)f->private_data;
	memset(internal, 0, sizeof(*internal));

	/* Set default terminal settings */
	internal->termios.c_lflag = 0000012; /* ECHO | ICANON */

	/* Inisialisasi window size default agar bash tidak menganggap ini terminal 0x0 */
	internal->ws.ws_col = 80;
	internal->ws.ws_row = 24;

	internal->id = slave_id;
	internal->locked = 1;
	internal->master_closed = 0;
	internal->slave_closed = 0;

	vnode->vnode_private = fd;

	slave_id++;

	print_dentry_tree(get_root_dentry(), 0);
}

vops_file_t ptmx_ops = {
    .redirect_on_open = ptmx_open,
};

INIT(PTY) {
	if (vxnamei("/dev/ptmx", &ptmx_dentry) != VFS_OK)
		return;

	dentry_ptr pts_dir;
	if (resolve_dentry("/dev/pts", 0, &pts_dir, CREATE_MISSING_ENTRY) == VFS_OK) {
		if (pts_dir && pts_dir->vnode) {
			pts_dir->vnode->type = VNODE_TYPE_DIR;
		}
	}

	vnode_ptr_t n = create_and_attach_vnode();
	n->type = VNODE_TYPE_DEV;
	n->ops = &ptmx_ops;
	ptmx_dentry->vnode = n;

	serial2_printf("pty has been setuped\n");
}