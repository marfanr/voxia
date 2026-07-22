#ifndef __TTY_TTY_H__
#define __TTY_TTY_H__

#include "termios.h"
#include <autoconf.h>
#include <spinlock.h>
#include <type.h>
#include <vfs/dentry.h>

struct thread;

#define TTY_INVERSE_FLAG_IDX 5

struct tty_cell {
	uint32_t codepoint;
	uint32_t fg;
	uint32_t bg;
};

struct tty_internal {
	boolean_t enable;
	boolean_t dirty;
	uint32_t cols;
	uint32_t rows;
	uint32_t cursorx;
	uint32_t cursory;
	uint32_t head;
	uint32_t tail;
	uint32_t line_buff_head;
	uint32_t line_buff_tail;
	uint32_t line_buff_cursor;
	spinlock_t tty_lock;
	struct thread* waiter;
	uint8_t ansi_state;
	uint8_t ansi_param_count;
	uint16_t ansi_params[16];
	uint32_t fg_color;
	uint32_t bg_color;

	boolean_t
	    alt_screen; /* true while inside \033[?1049h alternate screen */

	struct termios termios;

	/*
	 * _pad byte usage:
	 *   [0..1] = saved cursorx (lo/hi)
	 *   [2..3] = saved cursory (lo/hi)
	 *   [4]    = ACS charset active flag
	 *   [5]    = unused
	 *   [6]    = private-CSI flag ('?' seen in current CSI sequence)
	 *   [7]    = unused
	 */
	uint8_t _pad[8];

	char last_char[5];   /* Stores the last printed UTF-8/ACS character for
	                        REP (repeat) */
	uint8_t* backbuffer; /* Per-TTY pixel backbuffer for double buffering */
	struct tty_cell* cells; /* 2D grid of character cells (cols * rows) */
	struct tty_cell* alt_cells; /* Backup grid for alternate screen */
	uint32_t alt_cursorx;
	uint32_t alt_cursory;

	int forground_tid;
	int min_dirty_row;
	int max_dirty_row;

	uint32_t scroll_top;
	uint32_t scroll_bottom;

	char line_buff[1024];
	char input_buffer[VOXIA_TTY_INPUT_BUFFER_SIZE];
} __attribute__((aligned(64)));

void change_active_tty(int tty);
int get_active_tty();
int get_tty_id(struct tty_internal* priv);
dentry_ptr get_active_tty_dentry();
dentry_ptr get_tty_dentry(int tty);
void start_tty();
void tty_check_and_flush();
void tty_putchar_raw(struct tty_internal* priv, const char* s, int len);
void tty_putchar_raw_nolock(struct tty_internal* priv, const char* s, int len);
void tty_clear_area(struct tty_internal* priv, int x, int y, int w, int h);
void tty_clear_area_nolock(struct tty_internal* priv, int x, int y, int w,
                           int h);
void tty_redraw_screen_nolock(struct tty_internal* priv);
void codepoint_to_utf8(uint32_t cp, char* s);
void do_scroll_nolock(struct tty_internal* priv);
void do_scroll_down_nolock(struct tty_internal* priv);
void tty_mark_row_dirty(struct tty_internal* priv, int row);

// ioctl
struct win_size {
	uint16_t ws_row;
	uint16_t ws_col;
	uint16_t ws_xpixel;
	uint16_t ws_ypixel;
};

#endif // __TTY_TTY_H__