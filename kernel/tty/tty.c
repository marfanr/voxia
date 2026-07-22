#include "tty.h"
#include "ansi.h"
#include "hal/cpu/core.h"
#include "init/init.h"
#include "input.h"
#include "libk/serial.h"
#include "notify.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include "str.h"
#include "string.h"
#include "sys/err_no.h"
#include "sys/sig.h"
#include "sys/syscall.h"
#include "type.h"
#include "vfs/dentry.h"
#include "vfs/dev.h"
#include "vfs/ioctl.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <autoconf.h>
#include <console/console.h>
#include <cpu/irq_lock.h>
#include <graphic.h>

#define TIOCGWINSZ 0x5413
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

void tty_mark_row_dirty(struct tty_internal* priv, int row) {
	if (row < 0 || row >= (int)priv->rows)
		return;
	if (row < priv->min_dirty_row)
		priv->min_dirty_row = row;
	if (row > priv->max_dirty_row)
		priv->max_dirty_row = row;
}

static void tty_mark_all_dirty(struct tty_internal* priv) {
	priv->min_dirty_row = 0;
	priv->max_dirty_row = (int)priv->rows - 1;
}

static void tty_clear_dirty(struct tty_internal* priv) {
	priv->min_dirty_row = (int)priv->rows;
	priv->max_dirty_row = -1;
}

void do_scroll_nolock(struct tty_internal* priv) {
	if (priv->scroll_top == 0 && priv->scroll_bottom == priv->rows - 1) {
		vxScroll_nolock(g_font_size, priv->bg_color);
	} else {
		vxScrollRegion_nolock(g_font_size, (int)priv->scroll_top,
		                      (int)priv->scroll_bottom, priv->bg_color);
	}

	tty_mark_all_dirty(priv);

	if (priv->cells) {
		int row_size = (int)priv->cols;
		int start_cell = (int)priv->scroll_top * row_size;
		int move_cells =
		    ((int)priv->scroll_bottom - (int)priv->scroll_top) *
		    row_size;

		memmove(priv->cells + start_cell,
		        priv->cells + start_cell + row_size,
		        (size_t)move_cells * sizeof(struct tty_cell));

		struct tty_cell* new_line =
		    priv->cells + start_cell + move_cells;
		for (int i = 0; i < row_size; i++) {
			new_line[i].codepoint = ' ';
			new_line[i].fg = priv->fg_color;
			new_line[i].bg = priv->bg_color;
		}
	}
}

static void do_scroll(struct tty_internal* priv) {
	graphic_lock();
	do_scroll_nolock(priv);
	graphic_unlock();
}

void do_scroll_down_nolock(struct tty_internal* priv) {
	if (priv->scroll_top == 0 && priv->scroll_bottom == priv->rows - 1) {
		vxScrollDown_nolock(g_font_size, priv->bg_color);
	} else {
		vxScrollDownRegion_nolock(g_font_size, (int)priv->scroll_top,
		                          (int)priv->scroll_bottom,
		                          priv->bg_color);
	}

	tty_mark_all_dirty(priv);

	if (priv->cells) {
		int row_size = (int)priv->cols;
		int start_cell = (int)priv->scroll_top * row_size;
		int move_cells =
		    ((int)priv->scroll_bottom - (int)priv->scroll_top) *
		    row_size;

		memmove(priv->cells + start_cell + row_size,
		        priv->cells + start_cell,
		        (size_t)move_cells * sizeof(struct tty_cell));

		struct tty_cell* new_line = priv->cells + start_cell;
		for (int i = 0; i < row_size; i++) {
			new_line[i].codepoint = ' ';
			new_line[i].fg = priv->fg_color;
			new_line[i].bg = priv->bg_color;
		}
	}
}

void tty_putchar_raw_nolock(struct tty_internal* priv, const char* s, int len) {
	uint32_t cp = 0;
	if (len == 1) {
		cp = (uint8_t)s[0];
	} else {
		uint8_t* p = (uint8_t*)s;
		if (len == 2) {
			cp = (uint32_t)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
		} else if (len == 3) {
			cp = (uint32_t)(((p[0] & 0x0F) << 12) |
			                ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
		} else if (len == 4) {
			cp = (uint32_t)(((p[0] & 0x07) << 18) |
			                ((p[1] & 0x3F) << 12) |
			                ((p[2] & 0x3F) << 6) | (p[3] & 0x3F));
		}
	}

	uint32_t fg = priv->fg_color;
	uint32_t bg = priv->bg_color;
	if (priv->_pad[TTY_INVERSE_FLAG_IDX]) {
		uint32_t tmp = fg;
		fg = bg;
		bg = tmp;
	}

	int idx = (int)priv->cursory * (int)priv->cols + (int)priv->cursorx;
	if (priv->cells && idx >= 0 && idx < (int)(priv->cols * priv->rows)) {
		priv->cells[idx].codepoint = cp;
		priv->cells[idx].fg = fg;
		priv->cells[idx].bg = bg;
	}

	tty_mark_row_dirty(priv, (int)priv->cursory);

	if (len > 1) {
		putc_utf8_nolock(s, (int)priv->cursorx, (int)priv->cursory, fg,
		                 bg);
	} else {
		putc_nolock(s[0], (int)priv->cursorx, (int)priv->cursory, fg,
		            bg);
	}
	priv->cursorx++;
	if (priv->cursorx >= priv->cols) {
		priv->cursorx = 0;
		if (priv->cursory == priv->scroll_bottom) {
			do_scroll_nolock(priv);
		} else {
			priv->cursory++;
			if (priv->cursory >= priv->rows) {
				priv->cursory = priv->rows - 1;
			}
		}
	}
	if (len < 5) {
		for (int i = 0; i < len; i++) {
			priv->last_char[i] = s[i];
		}
		priv->last_char[len] = '\0';
	}
}

void tty_putchar_raw(struct tty_internal* priv, const char* s, int len) {
	graphic_lock();
	tty_putchar_raw_nolock(priv, s, len);
	graphic_unlock();
}

void codepoint_to_utf8(uint32_t cp, char* s) {
	if (cp < 0x80) {
		s[0] = (char)cp;
		s[1] = '\0';
	} else if (cp < 0x800) {
		s[0] = (char)(0xc0 | (cp >> 6));
		s[1] = (char)(0x80 | (cp & 0x3f));
		s[2] = '\0';
	} else if (cp < 0x10000) {
		s[0] = (char)(0xe0 | (cp >> 12));
		s[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
		s[2] = (char)(0x80 | (cp & 0x3f));
		s[3] = '\0';
	} else {
		s[0] = (char)(0xf0 | (cp >> 18));
		s[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
		s[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
		s[3] = (char)(0x80 | (cp & 0x3f));
		s[4] = '\0';
	}
}

void tty_clear_area_nolock(struct tty_internal* priv, int x, int y, int w,
                           int h) {
	for (int r = y; r < y + h; r++) {
		for (int c = x; c < x + w; c++) {
			if (c >= 0 && c < (int)priv->cols && r >= 0 &&
			    r < (int)priv->rows) {
				int idx = r * (int)priv->cols + c;
				if (priv->cells) {
					priv->cells[idx].codepoint = ' ';
					priv->cells[idx].fg = priv->fg_color;
					priv->cells[idx].bg = priv->bg_color;
				}
			}
		}
		tty_mark_row_dirty(priv, r);
	}
	fill_rect_nolock(x * (g_font_size / 2), y * g_font_size,
	                 w * (g_font_size / 2), h * g_font_size,
	                 priv->bg_color);
}

void tty_clear_area(struct tty_internal* priv, int x, int y, int w, int h) {
	graphic_lock();
	tty_clear_area_nolock(priv, x, y, w, h);
	graphic_unlock();
}

void tty_redraw_screen_nolock(struct tty_internal* priv) {
	if (!priv || !priv->cells)
		return;

	// Fill screen with default background first to wipe out previous
	// fragments
	fill_rect_nolock(0, 0, (int)priv->cols * (g_font_size / 2),
	                 (int)priv->rows * g_font_size, priv->bg_color);

	for (int y = 0; y < (int)priv->rows; y++) {
		for (int x = 0; x < (int)priv->cols; x++) {
			int idx = y * (int)priv->cols + x;
			uint32_t cp = priv->cells[idx].codepoint;
			uint32_t fg = priv->cells[idx].fg;
			uint32_t bg = priv->cells[idx].bg;

			if (cp == 0 || cp == ' ') {
				if (bg != priv->bg_color) {
					fill_rect_nolock(x * (g_font_size / 2),
					                 y * g_font_size,
					                 (g_font_size / 2),
					                 g_font_size, bg);
				}
			} else {
				if (cp < 0x80) {
					if (bg != priv->bg_color) {
						fill_rect_nolock(
						    x * (g_font_size / 2),
						    y * g_font_size,
						    (g_font_size / 2),
						    g_font_size, bg);
					}
					putc_nolock((char)cp, x, y, fg, bg);
				} else {
					char s[5];
					codepoint_to_utf8(cp, s);
					putc_utf8_nolock(s, x, y, fg, bg);
				}
			}
		}
		tty_mark_row_dirty(priv, y);
	}
}

static uint32_t line_buff_used(struct tty_internal* priv) {
	return (priv->line_buff_tail - priv->line_buff_head) & LINE_BUFF_MASK;
}

static void tty_erase_cursor_at_nolock(struct tty_internal* priv, int cx,
                                       int cy) {
	if (cx < 0 || cx >= (int)priv->cols || cy < 0 || cy >= (int)priv->rows)
		return;

	tty_mark_row_dirty(priv, cy);

	int idx = cy * (int)priv->cols + cx;
	uint32_t cp = 0;
	uint32_t fg = 0xFFFFFF;
	uint32_t bg = 0x000000;
	if (priv->cells) {
		cp = priv->cells[idx].codepoint;
		fg = priv->cells[idx].fg;
		bg = priv->cells[idx].bg;
	}

	int px_x = cx * (g_font_size / 2);
	int px_y = cy * g_font_size;

	fill_rect_nolock(px_x, px_y, g_font_size / 2, g_font_size, bg);

	if (cp >= 0x20) {
		char s[5];
		codepoint_to_utf8(cp, s);
		putc_utf8_nolock(s, cx, cy, fg, bg);
	}
}

static void tty_erase_cursor_nolock(struct tty_internal* priv) {
	tty_erase_cursor_at_nolock(priv, (int)priv->cursorx,
	                           (int)priv->cursory);
}

static void tty_draw_cursor_nolock(struct tty_internal* priv) {
	int px_x = (int)priv->cursorx * (g_font_size / 2);
	int px_y = (int)priv->cursory * g_font_size;
	tty_mark_row_dirty(priv, (int)priv->cursory);
	fill_rect_nolock(px_x, px_y, 2, g_font_size, 0xEEEEEE);
}

static void tty_redraw_from_nolock(struct tty_internal* priv, uint32_t buff_pos,
                                   uint32_t snap_tail, int screen_x,
                                   int screen_y) {
	int rx = screen_x;
	int ry = screen_y;

	int h = g_font_size;

	tty_mark_row_dirty(priv, ry);
	int clear_w = ((int)priv->cols - rx) * (g_font_size / 2);
	if (clear_w > 0)
		fill_rect_nolock(rx * (g_font_size / 2), ry * g_font_size,
		                 clear_w, h, 0x000000);

	uint32_t scan = buff_pos;
	while (scan != snap_tail) {
		char temp[3] = {0};
		int char_len = utf8_char_len((uint8_t)priv->line_buff[scan]);
		memcopy(temp, (uint8_t*)priv->line_buff + scan,
		        (size_t)char_len);
		putc_utf8_nolock(temp, rx, ry, 0xFFFFFF, 0x000000);
		rx++;
		if (rx >= (int)priv->cols) {
			rx = 0;
			ry++;
			if (ry >= (int)priv->rows)
				break;

			tty_mark_row_dirty(priv, ry);
			fill_rect_nolock(0, ry * g_font_size,
			                 (int)priv->cols * (g_font_size / 2), h,
			                 0x000000);
		}
		scan = (scan + 1) & LINE_BUFF_MASK;
	}

	if (ry < (int)priv->rows) {
		tty_mark_row_dirty(priv, ry);
		int tail_clear_w = ((int)priv->cols - rx) * (g_font_size / 2);
		if (tail_clear_w > 0)
			fill_rect_nolock(rx * (g_font_size / 2),
			                 ry * g_font_size, tail_clear_w, h,
			                 0x000000);
	}
}

__attribute__((unused)) static void
tty_redraw_from(struct tty_internal* priv, uint32_t buff_pos,
                uint32_t snap_tail, int screen_x, int screen_y) {
	graphic_lock();
	tty_redraw_from_nolock(priv, buff_pos, snap_tail, screen_x, screen_y);
	graphic_unlock();
}

static const char* translate_acs(uint8_t c) {
	switch (c) {
	case 'l':
		return "┌";
	case 'm':
		return "└";
	case 'k':
		return "┐";
	case 'j':
		return "┘";
	case 'q':
		return "─";
	case 'x':
		return "│";
	case 't':
		return "├";
	case 'u':
		return "┤";
	case 'v':
		return "┴";
	case 'w':
		return "┬";
	case 'n':
		return "┼";
	case 'a':
		return "▒";
	case 'f':
		return "°";
	case 'g':
		return "±";
	case 'y':
		return "≤";
	case 'z':
		return "≥";
	case '{':
		return "π";
	case '|':
		return "≠";
	case '}':
		return "£";
	case '~':
		return "·";
	default:
		return NULL;
	}
}

static void tty_do_flush(struct tty_internal* priv) {
	if (!priv || !priv->dirty)
		return;

	graphic_lock();

	graphic_set_draw_buffer(priv->backbuffer);
	tty_erase_cursor_nolock(priv);

	priv->dirty = false;

	if (priv->fg_color == 0 && priv->bg_color == 0) {
		priv->fg_color = 0xFFFFFF;
	}

	while (priv->head != priv->tail) {
		uint8_t c = (uint8_t)priv->input_buffer[priv->head];

		if (ansi_process_char(priv, c)) {
			priv->head = (priv->head + 1) &
			             (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);
			continue;
		}

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
				tty_putchar_raw_nolock(priv, seq, clen);
				priv->head = temp_head;
				continue;
			}
		}

		priv->head =
		    (priv->head + 1) & (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);

		if (c == '\n') {
			priv->cursorx = 0;
			if (priv->cursory == priv->scroll_bottom) {
				do_scroll_nolock(priv);
			} else {
				priv->cursory++;
				if (priv->cursory >= priv->rows) {
					priv->cursory = priv->rows - 1;
				}
			}
		} else if (c == '\r') {
			priv->cursorx = 0;
		} else if (c == '\b') {
			if (priv->cursorx > 0) {
				priv->cursorx--;
				tty_mark_row_dirty(priv, (int)priv->cursory);
				int idx = (int)priv->cursory * (int)priv->cols +
				          (int)priv->cursorx;
				if (priv->cells && idx >= 0 &&
				    idx < (int)(priv->cols * priv->rows)) {
					priv->cells[idx].codepoint = ' ';
					priv->cells[idx].fg = priv->fg_color;
					priv->cells[idx].bg = priv->bg_color;
				}
				putc_nolock(' ', (int)priv->cursorx,
				            (int)priv->cursory, priv->fg_color,
				            priv->bg_color);
			}
		} else if (c == '\t') {
			priv->cursorx += 4;
		} else {
			const char* acs_str = NULL;
			if (priv->_pad[4]) {
				acs_str = translate_acs(c);
			}
			if (acs_str) {
				tty_putchar_raw_nolock(priv, acs_str,
				                       (int)strlen(acs_str));
			} else {
				char temp[2] = {(char)c, '\0'};
				tty_putchar_raw_nolock(priv, temp, 1);
			}
		}
	}

	tty_draw_cursor_nolock(priv);

	graphic_set_draw_buffer(NULL);
	if (get_active_tty() == get_tty_id(priv)) {
		if (priv->min_dirty_row <= priv->max_dirty_row) {
			graphic_flush_backbuffer_rows_nolock(
			    priv->backbuffer, priv->min_dirty_row,
			    priv->max_dirty_row);
			tty_clear_dirty(priv);
		}
	}
	graphic_unlock();
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
	}

	tty_do_flush(priv);

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

__attribute__((unused)) static uint32_t
utf8_display_width(const char* codepoint) {
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
	bool is_key = input->type == INPUT_EVENT_KEY;

	if (!(is_text || is_key))
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

	if (!priv->waiter)
		return;


	uint32_t snap_tail;
	uint32_t redraw_from;
	int rx, ry;
	boolean_t do_redraw = false;
	boolean_t do_cursor_only = false;
	boolean_t do_newline = false;
	struct thread* wake_thread = NULL;

	int old_cx, old_cy;

	char translated_codepoint[2] = {0, 0};
	if (is_text) {
		size_t clen = strlen(codepoint);
		if (clen == 1) {
			char c = codepoint[0];
			if (c == '\r') {
				if (priv->termios.c_iflag & IGNCR) {
					return;
				} else if (priv->termios.c_iflag & ICRNL) {
					translated_codepoint[0] = '\n';
					codepoint = translated_codepoint;
				}
			} else if (c == '\n') {
				if (priv->termios.c_iflag & INLCR) {
					translated_codepoint[0] = '\r';
					codepoint = translated_codepoint;
				}
			}
		}
	}

	size_t codepoint_len = is_text ? strlen(codepoint) : 0;

	uintptr_t flags = irq_save();
	spin_acquire(&priv->tty_lock);

	bool is_sig_key = false;
	if ((priv->termios.c_lflag & ISIG) && is_text && codepoint_len == 1) {
		char c = codepoint[0];
		if (c == 3 || c == 26 || c == 28) {
			is_sig_key = true;
		}
	}

	old_cx = (int)priv->cursorx;
	old_cy = (int)priv->cursory;

	/* Signal keys check first */
	if (is_sig_key) {
		char c = codepoint[0];
		int sig = 0;
		if (c == 3)
			sig = SIGINT;
		else if (c == 26)
			sig = SIGTSTP;
		else if (c == 28)
			sig = SIGQUIT;

		if (sig > 0) {
			int target_pgid = priv->forground_tid;
			if (target_pgid == -1 && priv->waiter &&
			    priv->waiter->process) {
				target_pgid = (int)priv->waiter->process->pgid;
			}

			if (target_pgid > -1) {
				serial2_printf(
				    "TTY: Input handler signal %d for "
				    "PGID %d\n",
				    sig, target_pgid);

				pgid_send_signal((pid_t)target_pgid, sig);

				if (priv->waiter && priv->waiter->process &&
				    (int)priv->waiter->process->pgid ==
				        target_pgid) {
					serial2_printf(
					    "TTY: Waking waiter thread "
					    "%d\n",
					    priv->waiter->id);
					wake_thread = priv->waiter;
					priv->waiter = NULL;
				}

				do_newline = true;
			}

			goto done_input;
		}
	}

	/* Raw/pass-through mode when ICANON is disabled */
	if (!(priv->termios.c_lflag & ICANON)) {
		const char* raw_str = NULL;
		size_t raw_len = 0;

		if (is_text) {
			raw_str = codepoint;
			raw_len = codepoint_len;
		} else if (is_key) {
			if (input->key.keycode == KEY_DELETE) {
				raw_str = "\033[3~";
				raw_len = 4;
			} else if ((input->key.modifiers & MODIFIER_CTRL) &&
			           (input->key.modifiers & MODIFIER_ALT)) {
				raw_str = "\x02";
				raw_len = 1;
			}
		}

		if (raw_str && raw_len > 0) {
			uint32_t avail =
			    (LINE_BUFF_SIZE - 1) - line_buff_used(priv);
			if (raw_len <= avail) {
				for (size_t i = 0; i < raw_len; i++) {
					uint32_t pos =
					    (priv->line_buff_tail + i) &
					    LINE_BUFF_MASK;
					priv->line_buff[pos] = raw_str[i];
				}
				priv->line_buff_tail =
				    (priv->line_buff_tail + raw_len) &
				    LINE_BUFF_MASK;
				priv->line_buff_cursor = priv->line_buff_tail;
			}
		}

		if (priv->waiter) {
			wake_thread = priv->waiter;
			priv->waiter = NULL;
		}
		goto done_input;
	}

	/* Delete */
	if (is_key) {
		if (input->key.keycode == KEY_DELETE) {
			if (priv->line_buff_cursor == priv->line_buff_tail)
				goto done_input;

			uint32_t del_pos = priv->line_buff_cursor;
			uint32_t cp_len =
			    (uint32_t)utf8_codepoint_size_at(priv, del_pos);

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
			    (priv->line_buff_tail - 1) & LINE_BUFF_MASK;
			priv->line_buff_cursor = del_pos;

			if (priv->termios.c_lflag & ECHO) {
				redraw_from = del_pos;
				snap_tail = priv->line_buff_tail;
				rx = (int)priv->cursorx;
				ry = (int)priv->cursory;
				do_redraw = true;
			}
		}

		goto done_input;
	}

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

			if (priv->termios.c_lflag & ECHO) {
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
					if (priv->cursory ==
					    priv->scroll_bottom) {
						do_scroll(priv);
					} else {
						priv->cursory++;
						if (priv->cursory >=
						    priv->rows) {
							priv->cursory =
							    priv->rows - 1;
						}
					}
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
	/* Discard control characters in canonical mode */
	if (codepoint_len == 1) {
		char c = codepoint[0];
		if (((unsigned char)c < 32 || c == 127) && c != '\n' &&
		    c != '\r' && c != '\b' && c != '\t') {
			goto done_input;
		}
	}

	{
		uint32_t avail = (LINE_BUFF_SIZE - 1) - line_buff_used(priv);
		if (codepoint_len <= avail) {
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

			if (priv->termios.c_lflag & ECHO) {
				redraw_from = priv->line_buff_cursor;
				snap_tail = priv->line_buff_tail;
				rx = (int)priv->cursorx;
				ry = (int)priv->cursory;

				priv->cursorx +=
				    (uint32_t)utf8_display_width(codepoint);
				if (priv->cursorx >= priv->cols) {
					priv->cursorx = 0;
					if (priv->cursory ==
					    priv->scroll_bottom) {
						do_scroll(priv);
					} else {
						priv->cursory++;
						if (priv->cursory >=
						    priv->rows) {
							priv->cursory =
							    priv->rows - 1;
						}
					}
				}
				do_redraw = true;
			}

			priv->line_buff_cursor =
			    (priv->line_buff_cursor + codepoint_len) &
			    LINE_BUFF_MASK;
		}
	}

done_input:
	if (do_redraw) {
		graphic_lock();
		graphic_set_draw_buffer(priv->backbuffer);
		tty_erase_cursor_at_nolock(priv, old_cx, old_cy);
		serial2_flush();
		tty_redraw_from_nolock(priv, redraw_from, snap_tail, rx, ry);
		tty_draw_cursor_nolock(priv);
		graphic_set_draw_buffer(NULL);
		if (get_active_tty() == get_tty_id(priv)) {
			graphic_flush_backbuffer_rows_nolock(
			    priv->backbuffer, priv->min_dirty_row,
			    priv->max_dirty_row);
			tty_clear_dirty(priv);
		}
		graphic_unlock();
	} else if (do_cursor_only) {
		graphic_lock();
		graphic_set_draw_buffer(priv->backbuffer);
		tty_erase_cursor_at_nolock(priv, old_cx, old_cy);
		tty_draw_cursor_nolock(priv);
		graphic_set_draw_buffer(NULL);
		if (get_active_tty() == get_tty_id(priv)) {
			graphic_flush_backbuffer_rows_nolock(
			    priv->backbuffer, priv->min_dirty_row,
			    priv->max_dirty_row);
			tty_clear_dirty(priv);
		}
		graphic_unlock();
	}

	spin_release(&priv->tty_lock);
	irq_restore(flags);

	if (wake_thread) {
		vxThreadWake(wake_thread);
	}
	if (do_newline && !priv->alt_screen) {
		graphic_lock();
		graphic_set_draw_buffer(priv->backbuffer);
		tty_erase_cursor_nolock(priv);
		graphic_set_draw_buffer(NULL);
		if (get_active_tty() == get_tty_id(priv)) {
			graphic_flush_backbuffer_rows_nolock(
			    priv->backbuffer, priv->min_dirty_row,
			    priv->max_dirty_row);
			tty_clear_dirty(priv);
		}
		graphic_unlock();
		char_write(vnode, (void*)"\n", 1, 0);
	}
}
static int char_poll(vnode_t* vnode, thread_t* waiter) {
	struct tty_internal* priv = (struct tty_internal*)vnode->vnode_private;
	if (!priv)
		return 1;
	priv->waiter = waiter;
	if (priv->line_buff_head != priv->line_buff_tail)
		return 1;
	/* Clear any stale wake_pending so the upcoming thread_block actually
	 * sleeps */
	__atomic_store_n(&waiter->wake_pending, false, __ATOMIC_SEQ_CST);
	return 0;
}

INIT(TTY) {
	__tty_ops = (vops_file_t*)kalloc(sizeof(vops_file_t));
	__tty_ops->ioctl = char_ioctl;
	__tty_ops->write = char_write;
	__tty_ops->read = char_read;
	__tty_ops->poll = char_poll;

	for (int i = 0; i < VOXIA_TTY_MAX_COUNT; i++)
		configure_tty(i);

	// TODO: refer to active tty
	dentry_ptr tty_dentry;
	vxnamei("/dev/tty", &tty_dentry);
	tty_dentry->vnode = get_active_tty_dentry()->vnode;

	struct notifier* n = (struct notifier*)kalloc(sizeof(struct notifier));
	memset(n, 0, sizeof(struct notifier));
	n->callback = tty_input_handler;
	n->context = 0;
	n->priority = NOTIFY_HIGHT;
	n->flags = 0;
	notify_register("/input/triggered", n);
}

static int char_ioctl(vnode_t* vnode, uint32_t req, void* arg) {
	struct tty_internal* priv = (struct tty_internal*)vnode->vnode_private;

	switch (req) {
	case TIOCGWINSZ: {
		struct win_size* ws = (struct win_size*)arg;
		if (!ws)
			return -EBADF;

		ws->ws_row = priv ? (uint16_t)priv->rows : 24;
		ws->ws_col = priv ? (uint16_t)priv->cols : 80;
		ws->ws_xpixel = (uint16_t)vxGetWidth();
		ws->ws_ypixel = (uint16_t)vxGetHeight();
		return 0;
	}
	case TCGETS: // TCGETS
		if (arg && priv) {
			memcopy(arg, &priv->termios, sizeof(struct termios));
		}
		return 0;
	case TIOCSPGRP: { 
		serial2_printf("set foreground id %d\n", *(int*)arg);
		priv->forground_tid = *(int*)arg;
		return 0;
	}
	case TIOCGPGRP: { // tcgetpgrp (TIOCGPGRP)
		if (arg && priv) {
			*(int*)arg = priv->forground_tid;
		}
		return 0;
	}

	case 0x5402:   // TCSETS , now
	case 0x5403:   // TCSETSW, until output empty
	case 0x5404: { // TCSETSF, force remove unreaded output
		if (arg && priv) {
			memcopy(&priv->termios, arg, sizeof(struct termios));
		}
		serial2_printf("TCSET Ok\n");
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

	// handle active signals
	auto curr_thr = get_current_core_data()->active_thread;
	if (curr_thr && curr_thr->signal) {
		uint64_t pending = __atomic_load_n(
		    &curr_thr->signal->pending.__bits[0], __ATOMIC_ACQUIRE);
		uint64_t mask = __atomic_load_n(
		    &curr_thr->signal->mask.__bits[0], __ATOMIC_ACQUIRE);

		uint64_t active_signals = pending & ~mask;
		if (active_signals) {
			for (int sig = 1; sig <= 64; sig++) {
				if (!(active_signals & SIGBIT(sig)))
					continue;

				serial2_printf("TTY: char_read entry detected "
				               "signal %d on thread %d\n",
				               sig, curr_thr->id);
				sig_handle_ptr_t handler =
				    curr_thr->signal->handler[sig - 1];
				if (handler == 0) { // SIG_DFL
					if (sig == SIGINT || sig == SIGQUIT ||
					    sig == SIGKILL || sig == SIGTERM) {
						__atomic_fetch_and(
						    &curr_thr->signal->pending
						         .__bits[0],
						    ~SIGBIT(sig),
						    __ATOMIC_RELEASE);
						syscall_exit_group(128 + sig);
						return -EINTR;
					}
				} else if ((uintptr_t)handler == 1) { // SIG_IGN
					__atomic_fetch_and(
					    &curr_thr->signal->pending
					         .__bits[0],
					    ~SIGBIT(sig), __ATOMIC_RELEASE);
				}
				return -EINTR;
			}
		}
	}

	uint8_t* out = (uint8_t*)buf;
	size_t bytes_read = 0;

	while (1) {
		uintptr_t flags = irq_save();
		spin_acquire(&priv->tty_lock);

		if (priv->line_buff_head == priv->line_buff_tail) {
			if (curr_thr && curr_thr->signal) {
				uint64_t pending = __atomic_load_n(
				    &curr_thr->signal->pending.__bits[0],
				    __ATOMIC_ACQUIRE);
				uint64_t mask = __atomic_load_n(
				    &curr_thr->signal->mask.__bits[0],
				    __ATOMIC_ACQUIRE);
				uint64_t active_signals = pending & ~mask;
				if (active_signals) {
					for (int sig = 1; sig <= 64; sig++) {
						if (!(active_signals &
						      SIGBIT(sig)))
							continue;

						serial2_printf("TTY: char_read "
						               "wake detected "
						               "signal %d on "
						               "thread %d\n",
						               sig,
						               curr_thr->id);
						sig_handle_ptr_t handler =
						    curr_thr->signal
						        ->handler[sig - 1];
						if (handler == 0) { // SIG_DFL
							if (sig == SIGINT ||
							    sig == SIGQUIT ||
							    sig == SIGKILL ||
							    sig == SIGTERM) {
								__atomic_fetch_and(
								    &curr_thr
								         ->signal
								         ->pending
								         .__bits
								             [0],
								    ~SIGBIT(
								        sig),
								    __ATOMIC_RELEASE);
								if (priv->waiter ==
								    curr_thr) {
									priv->waiter =
									    NULL;
								}
								spin_release(
								    &priv->tty_lock);
								irq_restore(
								    flags);

								syscall_exit_group(
								    128 + sig);

								return -EINTR;
							}
						} else if ((uintptr_t)handler ==
						           1) { // SIG_IGN
							__atomic_fetch_and(
							    &curr_thr->signal
							         ->pending
							         .__bits[0],
							    ~SIGBIT(sig),
							    __ATOMIC_RELEASE);
						} else { // Custom
							 // handler
							if (priv->waiter ==
							    curr_thr) {
								priv->waiter =
								    NULL;
							}
							spin_release(
							    &priv->tty_lock);
							irq_restore(flags);
							return -EINTR;
						}
					}
				}
			}

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

			out[bytes_read++] = ch;
			if (ch == '\n' && (priv->termios.c_lflag & ICANON))
				break;
		}

		/* sync cursor position */
		priv->line_buff_cursor = priv->line_buff_head;

		if (priv->waiter == curr_thr) {
			priv->waiter = NULL;
		}

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
	priv->min_dirty_row = 0;
	priv->max_dirty_row = (int)priv->rows - 1;
	priv->cursorx = 0;
	priv->cursory = 0;
	priv->scroll_top = 0;
	priv->scroll_bottom = priv->rows - 1;
	priv->line_buff_tail = 0;
	priv->line_buff_head = 0;
	priv->line_buff_cursor = 0;
	priv->waiter = NULL;
	priv->tty_lock = (spinlock_t)SPINLOCK_INIT;
	priv->backbuffer = graphic_alloc_backbuffer();
	priv->cells = (struct tty_cell*)kalloc(priv->cols * priv->rows *
	                                       sizeof(struct tty_cell));
	if (priv->cells) {
		memset(priv->cells, 0,
		       priv->cols * priv->rows * sizeof(struct tty_cell));
	}
	priv->alt_cells = (struct tty_cell*)kalloc(priv->cols * priv->rows *
	                                           sizeof(struct tty_cell));
	if (priv->alt_cells) {
		memset(priv->alt_cells, 0,
		       priv->cols * priv->rows * sizeof(struct tty_cell));
	}

	priv->forground_tid = -1;
	priv->termios.c_iflag = ICRNL;
	priv->termios.c_lflag = ICANON | ECHO | ISIG;
}

void start_tty(void) {
	console_set_pos(0, 0);
	clear_screen(0x000000);
}

int get_tty_id(struct tty_internal* priv) {
	for (int i = 0; i < VOXIA_TTY_MAX_COUNT; i++) {
		if (__tty_dentry[i] && __tty_dentry[i]->vnode &&
		    __tty_dentry[i]->vnode->vnode_private == priv) {
			return i;
		}
	}
	return -1;
}

void change_active_tty(int tty) {
	if (tty >= 0 && tty < VOXIA_TTY_MAX_COUNT) {
		__current_tty_active = tty;
		if (__tty_dentry[tty] && __tty_dentry[tty]->vnode) {
			struct tty_internal* priv =
			    (struct tty_internal*)__tty_dentry[tty]
			        ->vnode->vnode_private;
			if (priv && priv->backbuffer) {
				tty_mark_all_dirty(priv);
				graphic_lock();
				if (priv->min_dirty_row <=
				    priv->max_dirty_row) {
					graphic_flush_backbuffer_rows_nolock(
					    priv->backbuffer,
					    priv->min_dirty_row,
					    priv->max_dirty_row);
					tty_clear_dirty(priv);
				}
				graphic_unlock();
			}
		}
	}
}
int get_active_tty(void) { return __current_tty_active; }

dentry_ptr get_active_tty_dentry(void) {
	return __tty_dentry[__current_tty_active];
}

void tty_check_and_flush(void) {
	// Reverted
}

dentry_ptr get_tty_dentry(int tty) { return __tty_dentry[tty]; }