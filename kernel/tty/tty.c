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
#define LINE_BUFF_SIZE 1024
#define LINE_BUFF_MASK (LINE_BUFF_SIZE - 1)

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

static uint32_t line_buff_used(struct tty_internal* priv) {
	return (priv->line_buff_tail - priv->line_buff_head) & LINE_BUFF_MASK;
}

static boolean_t line_buff_has_newline(struct tty_internal* priv) {
	uint32_t scan = priv->line_buff_head;
	while (scan != priv->line_buff_tail) {
		if ((uint8_t)priv->line_buff[scan] == '\n')
			return true;
		scan = (scan + 1) & LINE_BUFF_MASK;
	}
	return false;
}

static void tty_erase_cursor_at(struct tty_internal* priv, int cx, int cy,
                                uint32_t buff_pos) {
	int px_x = cx * (FONT_SIZE / 2);
	int px_y = cy * FONT_SIZE;

	fill_rect(px_x, px_y, FONT_SIZE / 2, FONT_SIZE + 1, 0x000000);

	if (buff_pos != priv->line_buff_tail &&
	    (uint8_t)priv->line_buff[buff_pos] >= 0x20)
		putc((char)priv->line_buff[buff_pos], cx, cy, 0xFFFFFF,
		     0x000000);
}

static void tty_erase_cursor(struct tty_internal* priv) {
	tty_erase_cursor_at(priv, (int)priv->cursorx, (int)priv->cursory,
	                    priv->line_buff_cursor);
}

static void tty_draw_cursor(struct tty_internal* priv) {
	int px_x = (int)priv->cursorx * (FONT_SIZE / 2);
	int px_y = (int)priv->cursory * FONT_SIZE;
	fill_rect(px_x, px_y, 2, FONT_SIZE + 1, 0xEEEEEE);
}

__attribute__((unused))
static void tty_redraw_from(struct tty_internal* priv, uint32_t buff_pos,
                            uint32_t snap_tail, int screen_x, int screen_y) {
	int rx = screen_x;
	int ry = screen_y;
	(void)rx;
	(void)ry;
	(void)priv;
	serial2_printf("tty_redraw_from: priv=0x%x, buff_pos=%d, snap_tail=%d\n", priv, buff_pos, snap_tail);
	int h = FONT_SIZE + 1;

	int clear_w = ((int)priv->cols - rx) * (FONT_SIZE / 2);
	if (clear_w > 0)
		fill_rect(rx * (FONT_SIZE / 2), ry * FONT_SIZE, clear_w, h,
		          0x000000);

	uint32_t scan = buff_pos;
	while (scan != snap_tail) {
		char temp[3] = {0};
		int char_len = utf8_char_len((uint8_t)priv->line_buff[scan]);
		memcopy(temp, (uint8_t*)priv->line_buff + scan, (size_t)char_len);
		putc_utf8(temp, rx, ry, 0xFFFFFF, 0x000000);
		rx++;
		if (rx >= (int)priv->cols) {
			rx = 0;
			ry++;
			if (ry >= (int)priv->rows)
				break;

			fill_rect(0, ry * FONT_SIZE,
			          (int)priv->cols * (FONT_SIZE / 2), h,
			          0x000000);
		}
		scan = (scan + 1) & LINE_BUFF_MASK;
	}

	if (ry < (int)priv->rows) {
		int tail_clear_w = ((int)priv->cols - rx) * (FONT_SIZE / 2);
		if (tail_clear_w > 0)
			fill_rect(rx * (FONT_SIZE / 2), ry * FONT_SIZE,
			          tail_clear_w, h, 0x000000);
	}
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
				priv->cursorx += (uint32_t)utf8_char_len(c);
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

static long char_write(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	UNUSED(offset);

	struct tty_internal* priv = (struct tty_internal*)vnode->vnode_private;
	if (!priv || !priv->enable)
		return -ENOENT;
	if (!len)
		return 0;

	uintptr_t flags = irq_save();
	spin_acquire(&priv->tty_lock);

	size_t remaining = len;
	uint8_t* p = (uint8_t*)buf;

	while (remaining > 0) {
		size_t used = (priv->tail - priv->head) &
		              (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);
		size_t avail = VOXIA_TTY_INPUT_BUFFER_SIZE - 1 - used;

		if (avail == 0) {
			priv->dirty = true;
			tty_do_flush(priv);
			used = (priv->tail - priv->head) &
			       (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);
			avail = VOXIA_TTY_INPUT_BUFFER_SIZE - 1 - used;
			if (avail == 0)
				break;
		}

		size_t to_copy = (remaining < avail) ? remaining : avail;
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

	spin_release(&priv->tty_lock);
	irq_restore(flags);

	return (long)(len - remaining);
}

static int utf8_codepoint_size_at(struct tty_internal* priv, uint32_t pos) {
	uint8_t c = (uint8_t)priv->line_buff[pos];
	return utf8_char_len(c);
}

static uint32_t utf8_prev_codepoint(struct tty_internal* priv, uint32_t pos) {
	uint32_t p = (pos - 1) & LINE_BUFF_MASK;

	while (p != priv->line_buff_head) {
		uint8_t c = (uint8_t)priv->line_buff[p];

		if ((c & 0xC0) != 0x80)
			break;

		p = (p - 1) & LINE_BUFF_MASK;
	}

	return p;
}

__attribute__((unused))
static uint32_t utf8_display_width(const char* codepoint) {
	size_t len = strlen(codepoint);

	if (len >= 3)
		return 2;

	return 1;
}

static void tty_input_handler(uint32_t event, void* data, void* ctx) {
	(void)event;
	(void)ctx;

	if (!data)
		return;

	struct input_event_data* input = (struct input_event_data*)data;

	bool is_text = input->type == INPUT_EVENT_TEXT;

	bool is_delete =
	    input->type == INPUT_EVENT_KEY && input->key.keycode == KEY_DELETE;

	if (!(is_text || is_delete))
		return;

	const char* codepoint = input->text.codepoint;
	if (is_text && !codepoint)
		return;

	struct dentry* dentry = get_active_tty_dentry();
	if (!dentry)
		return;
	struct vnode* vnode = dentry->vnode;
	if (!vnode)
		return;

	struct tty_internal* priv = (struct tty_internal*)vnode->vnode_private;
	if (!priv)
		return;

	uint32_t snap_tail;
	uint32_t redraw_from;
	int rx, ry;
	boolean_t do_redraw = false;
	boolean_t do_cursor_only = false;
	boolean_t do_newline = false;
	struct thread* wake_thread = NULL;

	int old_cx, old_cy;
	uint32_t old_cursor_buff;

	uintptr_t flags = irq_save();
	spin_acquire(&priv->tty_lock);

	old_cx = (int)priv->cursorx;
	old_cy = (int)priv->cursory;
	old_cursor_buff = priv->line_buff_cursor;

	/* Delete */
	if (is_delete) {
		serial2_printf("delete detected\n");

		if (priv->line_buff_cursor == priv->line_buff_tail)
			goto done_input;

		uint32_t del_pos = priv->line_buff_cursor;
		uint32_t cp_len =
		    (uint32_t)utf8_codepoint_size_at(priv, del_pos);

		uint32_t scan = del_pos;
		while (scan != priv->line_buff_tail) {
			uint32_t next = (scan + cp_len) & LINE_BUFF_MASK;
			if (next == priv->line_buff_head)
				break;
			priv->line_buff[scan] = priv->line_buff[next];
			scan = (scan + 1) & LINE_BUFF_MASK;
		}
		priv->line_buff_tail =
		    (priv->line_buff_tail - 1) & LINE_BUFF_MASK;
		priv->line_buff_cursor = del_pos;

		redraw_from = del_pos;
		snap_tail = priv->line_buff_tail;
		rx = (int)priv->cursorx;
		ry = (int)priv->cursory;
		do_redraw = true;

		goto done_input;
	}

	size_t codepoint_len = strlen(codepoint);

	/* Backspace */
	if (codepoint_len == 1 && codepoint[0] == '\b') {
		if (priv->line_buff_cursor != priv->line_buff_head) {
			uint32_t del_pos =
			    utf8_prev_codepoint(priv, priv->line_buff_cursor);
			uint32_t cp_len =
			    (uint32_t)utf8_codepoint_size_at(priv, del_pos);

			uint8_t del_byte = (uint8_t)priv->line_buff[del_pos];
			uint32_t del_width =
			    (utf8_char_len(del_byte) >= 3) ? 2 : 1;

			uint32_t scan = del_pos;
			while (scan != priv->line_buff_tail) {
				uint32_t next =
				    (scan + cp_len) & LINE_BUFF_MASK;

				if (next == priv->line_buff_head)
					break;

				priv->line_buff[scan] = priv->line_buff[next];
				scan = (scan + 1) & LINE_BUFF_MASK;
			}
			priv->line_buff_tail =
			    (priv->line_buff_tail - cp_len) & LINE_BUFF_MASK;
			priv->line_buff_cursor = del_pos;

			if (priv->cursorx >= del_width) {
				priv->cursorx -= del_width;
			} else if (priv->cursory > 0) {
				priv->cursory--;
				priv->cursorx = priv->cols - del_width;
			}

			redraw_from = del_pos;
			snap_tail = priv->line_buff_tail;
			rx = (int)priv->cursorx;
			ry = (int)priv->cursory;
			do_redraw = true;
		}
		goto done_input;
	}

	/* Newline */
	if (codepoint_len == 1 &&
	    (codepoint[0] == '\r' || codepoint[0] == '\n')) {
		uint32_t next_tail =
		    (priv->line_buff_tail + 1) & LINE_BUFF_MASK;
		if (next_tail != priv->line_buff_head) {
			priv->line_buff[priv->line_buff_tail] = '\n';
			priv->line_buff_tail = next_tail;
			priv->line_buff_cursor = next_tail;
		}
		if (priv->waiter) {
			wake_thread = priv->waiter;
			priv->waiter = NULL;
		}
		do_newline = true;
		goto done_input;
	}

	/* Escape sequence (arrow keys) */
	if (codepoint_len >= 3 && codepoint[0] == '\033' &&
	    codepoint[1] == '[') {
		switch (codepoint[2]) {
		case 'D': /* LEFT */
			if (priv->line_buff_cursor != priv->line_buff_head) {
				priv->line_buff_cursor = utf8_prev_codepoint(
				    priv, priv->line_buff_cursor);
				if (priv->cursorx > 0) {
					priv->cursorx--;
				} else if (priv->cursory > 0) {
					priv->cursory--;
					priv->cursorx = priv->cols - 1;
				}
			}
			break;
		case 'C': /* RIGHT */
			if (priv->line_buff_cursor != priv->line_buff_tail) {
				uint32_t cp_len =
				    (uint32_t)utf8_codepoint_size_at(
				        priv, priv->line_buff_cursor);

				priv->line_buff_cursor =
				    (priv->line_buff_cursor + cp_len) &
				    LINE_BUFF_MASK;
				priv->cursorx++;
				if (priv->cursorx >= priv->cols) {
					priv->cursorx = 0;
					priv->cursory++;
					if (priv->cursory >= priv->rows)
						do_scroll(priv);
				}
			}
			break;
		case 'A':
			break;
		case 'B':
			break;
		}
		do_cursor_only = true;
		goto done_input;
	}

	{
		uint32_t avail = (LINE_BUFF_SIZE - 1) - line_buff_used(priv);
		if (codepoint_len <= avail) {
			/* Geser karakter dari tail mundur untuk buat ruang */
			uint32_t scan = priv->line_buff_tail;
			while (scan != priv->line_buff_cursor) {
				uint32_t prev = (scan - 1) & LINE_BUFF_MASK;
				priv->line_buff[scan] = priv->line_buff[prev];
				scan = prev;
			}
			for (size_t i = 0; i < codepoint_len; i++) {
				uint32_t pos = (priv->line_buff_cursor + i) &
				               LINE_BUFF_MASK;
				priv->line_buff[pos] = codepoint[i];
			}
			priv->line_buff_tail =
			    (priv->line_buff_tail + codepoint_len) &
			    LINE_BUFF_MASK;

			redraw_from = priv->line_buff_cursor;
			snap_tail = priv->line_buff_tail;
			rx = (int)priv->cursorx;
			ry = (int)priv->cursory;

			priv->line_buff_cursor =
			    (priv->line_buff_cursor + codepoint_len) &
			    LINE_BUFF_MASK;
			priv->cursorx +=
			    (uint32_t)utf8_display_width(codepoint);
			if (priv->cursorx >= priv->cols) {
				priv->cursorx = 0;
				priv->cursory++;
				if (priv->cursory >= priv->rows)
					do_scroll(priv);
			}
			do_redraw = true;
		}
	}

done_input:
	/* Render while holding the lock to protect priv->line_buff from races */
	if (do_redraw) {
		tty_erase_cursor_at(priv, old_cx, old_cy, old_cursor_buff);
		serial2_printf("redrawing from %d to %d (0x%x)\n", redraw_from, snap_tail, priv->line_buff);
		serial2_flush();
		tty_redraw_from(priv, redraw_from, snap_tail, rx, ry);
		tty_draw_cursor(priv);
	} else if (do_cursor_only) {
		tty_erase_cursor_at(priv, old_cx, old_cy, old_cursor_buff);
		tty_draw_cursor(priv);
	}

	spin_release(&priv->tty_lock);
	irq_restore(flags);

	if (wake_thread)
		vxThreadWake(wake_thread);

	if (do_newline) {
		tty_erase_cursor(priv);
		char_write(vnode, (void*)"\n", 1, 0);
	}
}

INIT(TTY) {
	__tty_ops = (vops_file_t*)kalloc(sizeof(vops_file_t));
	__tty_ops->ioctl = char_ioctl;
	__tty_ops->write = char_write;
	__tty_ops->read = char_read;

	for (int i = 0; i < VOXIA_TTY_MAX_COUNT; i++)
		configure_tty(i);

	struct notifier* n = (struct notifier*)kalloc(sizeof(struct notifier));
	memset(n, 0, sizeof(struct notifier));
	n->callback = tty_input_handler;
	n->context = 0;
	n->priority = NOTIFY_HIGHT;
	n->flags = 0;
	notify_register("/input/triggered", n);
}

static int char_ioctl(vnode_t* vnode, uint32_t req, void* arg) {
	switch (req) {
	case TIOCGWINSZ: {
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

	while (1) {
		uintptr_t flags = irq_save();
		spin_acquire(&priv->tty_lock);

		if (!line_buff_has_newline(priv)) {
			priv->waiter = get_current_core_data()->active_thread;
			spin_release(&priv->tty_lock);
			irq_restore(flags);
			thread_block();
			continue;
		}

		while (bytes_read < len &&
		       priv->line_buff_head != priv->line_buff_tail) {
			uint8_t ch =
			    (uint8_t)priv->line_buff[priv->line_buff_head];
			priv->line_buff_head =
			    (priv->line_buff_head + 1) & LINE_BUFF_MASK;

			if (ch == '\r')
				ch = '\n';
			out[bytes_read++] = ch;
			if (ch == '\n')
				break;
		}

		/* sync cursor position */
		priv->line_buff_cursor = priv->line_buff_head;

		spin_release(&priv->tty_lock);
		irq_restore(flags);
		break;
	}

	return (int)bytes_read;
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
	priv->line_buff_cursor = 0;
	priv->waiter = NULL;
	priv->writer_waiter = NULL;
	priv->tty_lock = (spinlock_t)SPINLOCK_INIT;
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

void tty_check_and_flush(void) {
	dentry_ptr dentry = get_active_tty_dentry();
	if (!dentry)
		return;

	struct tty_internal* priv =
	    (struct tty_internal*)dentry->vnode->vnode_private;

	uintptr_t flags = irq_save();
	spin_acquire(&priv->tty_lock);
	tty_do_flush(priv);
	spin_release(&priv->tty_lock);
	irq_restore(flags);
}

dentry_ptr get_tty_dentry(int tty) { return __tty_dentry[tty]; }