#include "tty.h"
#include "hal/cpu/core.h"
#include "hal/cpu/irq_lock.h"
#include "hal/graphic/graphic.h"
#include "init/init.h"
#include "input.h"
#include "libk/serial.h"
#include "notify.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include "procc/workqueue.h"
#include "str.h"
#include "string.h"
#include "sys/err_no.h"
#include "type.h"
#include "vfs/dentry.h"
#include "vfs/dev.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <autoconf.h>
#include <console/console.h>

#define TIOCGWINSZ 0x5413
#define FONT_SIZE 14

// forward declarations
static void configure_tty(int tty);
static int char_ioctl(vnode_t* vnode, uint32_t req, void* arg);
static long char_write(vnode_t* vnode, void* buf, size_t len, size_t offset);
static void do_scroll(struct tty_internal* priv);
static int char_read(vnode_t* vnode, void* buf, size_t len, size_t offset);
static void tty_do_flush(struct tty_internal* priv);

static dentry_ptr __tty_dentry[VOXIA_TTY_MAX_COUNT] = {0};
static int __current_tty_active = 0;
static vops_file_t* __tty_ops = 0;

static void do_scroll(struct tty_internal* priv) {
	vxScroll(FONT_SIZE);
	priv->cursory = priv->rows - 1;
	priv->cursorx = 0;
}

static void tty_input_handler(uint32_t event, void* data, void* ctx) {
	(void)event;
	(void)ctx;

	if (!data)
		return;

	struct input_event_data* input = (struct input_event_data*)data;

	struct dentry* dentry = get_active_tty_dentry();
	if (!dentry)
		return;

	struct vnode* vnode = dentry->vnode;
	if (!vnode)
		return;

	struct tty_internal* priv = (struct tty_internal*)vnode->vnode_private;
	if (!priv)
		return;

	uint8_t c = (uint8_t)input->code;

	uintptr_t flags = irq_save();
	spin_acquire(&priv->output_lock);
	spin_acquire(&priv->input_lock);

	if (c == '\b' || c == 0x7F) {
		if (priv->line_buff_tail != priv->line_buff_head) {

			priv->line_buff_tail =
			    (priv->line_buff_tail - 1) & (1024 - 1);

			if (priv->cursorx > 0) {
				priv->cursorx--;
			} else if (priv->cursory > 0) {
				priv->cursory--;
				priv->cursorx = priv->cols - 1;
			}

			int px_x = (int)priv->cursorx * (FONT_SIZE / 2);
			int px_y = (int)priv->cursory * FONT_SIZE;
			fill_rect(px_x, px_y, FONT_SIZE / 2, FONT_SIZE + 5,
			          0x000000);
		}
		spin_release(&priv->input_lock);
		spin_release(&priv->output_lock);
		irq_restore(flags);
		return;
	}

	// handle new line
	if (c == '\r' || c == '\n') {
		priv->cursorx = 0;
		priv->cursory++;
		if (priv->cursory >= priv->rows)
			do_scroll(priv);

		/* Simpan \n ke line_buff lalu wake char_read */
		priv->line_buff[priv->line_buff_tail] = '\n';
		priv->line_buff_tail = (priv->line_buff_tail + 1) & (1024 - 1);

		if (priv->waiter) {
			vxThreadWake(priv->waiter);
			priv->waiter = NULL;
		}

		// print into latest cursor position
	} else if (c >= 0x20 && c < 0x7F) {
		putc((char)c, (int)priv->cursorx, (int)priv->cursory, 0xFFFFFF,
		     0x000000);
		priv->cursorx++;
		if (priv->cursorx >= priv->cols) {
			priv->cursorx = 0;
			priv->cursory++;
			if (priv->cursory >= priv->rows)
				do_scroll(priv);
		}

		priv->line_buff[priv->line_buff_tail] = (char)c;
		priv->line_buff_tail = (priv->line_buff_tail + 1) & (1024 - 1);
	}

	spin_release(&priv->input_lock);
	spin_release(&priv->output_lock);
	irq_restore(flags);
}

INIT(TTY) {
	__tty_ops = (vops_file_t*)kalloc(sizeof(vops_file_t));
	__tty_ops->ioctl = char_ioctl;
	__tty_ops->write = char_write;
	__tty_ops->read = char_read;

	for (int i = 0; i < VOXIA_TTY_MAX_COUNT; i++) {
		configure_tty(i);
	}

	// notify
	{
		struct notifier* n =
		    (struct notifier*)kalloc(sizeof(struct notifier));
		memset(n, 0, sizeof(struct notifier));
		n->callback = tty_input_handler;
		n->context = 0;
		n->priority = NOTIFY_HIGHT;
		n->flags = 0;

		notify_register("/input/trigered", n);
	}
}

static int char_ioctl(vnode_t* vnode, uint32_t req, void* arg) {
	switch (req) {
	case TIOCGWINSZ: {
		serial2_printf("ioctl: win size request\n");
		struct win_size* ws = (struct win_size*)arg;
		if (!ws)
			return -EBADF;

		struct tty_internal* priv =
		    (struct tty_internal*)vnode->vnode_private;
		ws->ws_row = priv ? (uint16_t)priv->rows : 24;
		ws->ws_col = priv ? (uint16_t)priv->cols : 80;
		ws->ws_xpixel = (uint16_t)vxGetWidth();
		ws->ws_ypixel = (uint16_t)vxGetHeight();
		return 0;
	}
	}

	return -ENOTTY;
}

static int char_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	(void)offset;

	if (!vnode || !buf)
		return -EINVAL;

	struct tty_internal* priv = (struct tty_internal*)vnode->vnode_private;
	if (!priv)
		return -ENOSYS;

	uint8_t* out = (uint8_t*)buf;
	size_t bytes_read = 0;

	/*
	 * wait until newline exist on line_buffer
	 */
	while (1) {
		uintptr_t flags = irq_save();
		spin_acquire(&priv->input_lock);

		boolean_t has_newline = false;
		uint32_t scan = priv->line_buff_head;
		while (scan != priv->line_buff_tail) {
			if (priv->line_buff[scan] == '\n' ||
			    priv->line_buff[scan] == '\r') {
				has_newline = true;
				break;
			}
			scan = (scan + 1) & (1024 - 1);
		}

		if (!has_newline) {
			priv->waiter = get_current_core_data()->active_thread;
			spin_release(&priv->input_lock);
			irq_restore(flags);

			thread_block();
			continue;
		}

		/* read until new line */
		while (bytes_read < len &&
		       priv->line_buff_head != priv->line_buff_tail) {
			uint8_t ch =
			    (uint8_t)priv->line_buff[priv->line_buff_head];
			priv->line_buff_head =
			    (priv->line_buff_head + 1) & (1024 - 1);

			if (ch == '\r')
				ch = '\n';

			out[bytes_read++] = ch;

			if (ch == '\n')
				break;
		}

		spin_release(&priv->input_lock);
		irq_restore(flags);
		break;
	}

	return (int)bytes_read;
}

static long char_write(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	UNUSED(offset);

	struct tty_internal* priv = (struct tty_internal*)vnode->vnode_private;
	if (!priv || !priv->enable)
		return -ENOENT;

	if (!len)
		return 0;

	uintptr_t flags = irq_save();
	spin_acquire(&priv->output_lock);

	size_t remaining = len;
	uint8_t* p = (uint8_t*)buf;

	while (remaining > 0) {
		size_t used = (priv->tail - priv->head) &
		              (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);
		size_t available = VOXIA_TTY_INPUT_BUFFER_SIZE - 1 - used;

		if (available == 0) {
			priv->dirty = true;
			tty_do_flush(priv);
			used = (priv->tail - priv->head) &
			       (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);
			available = VOXIA_TTY_INPUT_BUFFER_SIZE - 1 - used;
			if (available == 0)
				break;
		}

		size_t to_copy =
		    (remaining < available) ? remaining : available;
		size_t tail = priv->tail;
		size_t first_chunk = VOXIA_TTY_INPUT_BUFFER_SIZE - tail;

		if (to_copy <= first_chunk) {
			memcopy((uint8_t*)priv->input_buffer + tail, p,
			        to_copy);
		} else {
			memcopy((uint8_t*)priv->input_buffer + tail, p,
			        first_chunk);
			memcopy((uint8_t*)priv->input_buffer, p + first_chunk,
			        to_copy - first_chunk);
		}

		priv->tail =
		    (tail + to_copy) & (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);
		p += to_copy;
		remaining -= to_copy;

		priv->dirty = true;
		tty_do_flush(priv);
	}

	spin_release(&priv->output_lock);
	irq_restore(flags);

	return (long)(len - remaining);
}

static void configure_tty(int tty) {
	dentry_ptr* curr_dentry = &__tty_dentry[tty];
	kstring path_name = str_concat(str("/dev/tty"), itoa(tty, 10));
	vxnamei(path_name->c_str, curr_dentry);
	str_release(path_name);

	vnode_ptr_t vnode = create_and_attach_vnode();
	vnode->type = VNODE_TYPE_CHR;

	(*curr_dentry)->vnode = vnode;
	(*curr_dentry)->vnode->ops = __tty_ops;
	(*curr_dentry)->vnode->permission = 660;

	auto dev = create_dev(__tty_ops, DEV_MAJOR_TTY);
	(*curr_dentry)->vnode->device.major = dev->major;
	(*curr_dentry)->vnode->device.minor = dev->minor;
	(*curr_dentry)->vnode->mountedhere = dev;

	struct tty_internal* priv =
	    (struct tty_internal*)kalloc(sizeof(struct tty_internal));
	memset(priv, 0, sizeof(struct tty_internal));
	(*curr_dentry)->vnode->vnode_private = priv;

	priv->enable = true;
	priv->dirty = false;
	priv->cols = screen_cols();
	priv->rows = screen_rows();
	priv->cursorx = 0;
	priv->cursory = 0;
	priv->line_buff_tail = 0;
	priv->line_buff_head = 0;
	priv->waiter = NULL;
	priv->writer_waiter = NULL;
	priv->input_lock = (spinlock_t)SPINLOCK_INIT;
	priv->output_lock = (spinlock_t)SPINLOCK_INIT;
}

void start_tty(void) {
	console_set_pos(0, 0);
	clear_screen(0x000000);
}

void change_active_tty(int tty) { __current_tty_active = tty; }

int get_active_tty(void) { return __current_tty_active; }

dentry_ptr get_active_tty_dentry(void) {
	return __tty_dentry[__current_tty_active];
}

static void tty_do_flush(struct tty_internal* priv) {
	if (!priv || !priv->dirty)
		return;

	priv->dirty = false;

	while (priv->head != priv->tail) {
		uint8_t c = (uint8_t)priv->input_buffer[priv->head];
		int clen = utf8_char_len(c);

		if (clen > 1) {
			char seq[5] = {0};
			uint32_t temp_head = priv->head;
			boolean_t complete = true;
			for (int i = 0; i < clen; i++) {
				seq[i] = priv->input_buffer[temp_head];
				temp_head = (temp_head + 1) &
				            (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);
				if (temp_head == priv->tail && i < clen - 1) {
					complete = false;
					break;
				}
			}

			if (complete) {
				putc_utf8(seq, (int)priv->cursorx,
				          (int)priv->cursory, 0xFFFFFF,
				          0x000000);
				uint32_t width = (clen >= 3) ? 2 : 1;
				priv->cursorx += width;
				if (priv->cursorx >= priv->cols) {
					priv->cursorx = 0;
					priv->cursory++;
					if (priv->cursory >= priv->rows)
						do_scroll(priv);
				}
				priv->head = temp_head;
				continue;
			}
		}

		priv->head =
		    (priv->head + 1) & (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);

		if (c == '\n') {
			priv->cursorx = 0;
			priv->cursory++;
			if (priv->cursory >= priv->rows)
				do_scroll(priv);

		} else if (c == '\r') {
			priv->cursorx = 0;

		} else if (c == '\b') {
			if (priv->cursorx > 0) {
				priv->cursorx--;
				putc(' ', (int)priv->cursorx,
				     (int)priv->cursory, 0xFFFFFF, 0x000000);
			}
		} else {
			putc((char)c, (int)priv->cursorx, (int)priv->cursory,
			     0xFFFFFF, 0x000000);
			priv->cursorx++;

			if (priv->cursorx >= priv->cols) {
				priv->cursorx = 0;
				priv->cursory++;
				if (priv->cursory >= priv->rows)
					do_scroll(priv);
			}
		}
	}
}

void tty_check_and_flush(void) {
	dentry_ptr dentry = get_active_tty_dentry();
	if (!dentry)
		return;

	struct tty_internal* priv =
	    (struct tty_internal*)dentry->vnode->vnode_private;

	uintptr_t flags = irq_save();
	spin_acquire(&priv->output_lock);
	tty_do_flush(priv);
	spin_release(&priv->output_lock);
	irq_restore(flags);
}

dentry_ptr get_tty_dentry(int tty) { return __tty_dentry[tty]; }