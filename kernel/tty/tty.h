#ifndef __TTY_TTY_H__
#define __TTY_TTY_H__

#include "spinlock.h"
#include "vfs/dentry.h"
#include <autoconf.h>
#include <type.h>

struct thread;
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
	uint8_t _pad[8];

	char line_buff[1024];
	char input_buffer[VOXIA_TTY_INPUT_BUFFER_SIZE];
} __attribute__((aligned(64)));

void change_active_tty(int tty);
int get_active_tty();
dentry_ptr get_active_tty_dentry();
dentry_ptr get_tty_dentry(int tty);
void start_tty();
void tty_check_and_flush();


// ioctl
struct win_size {
	uint16_t ws_row;
	uint16_t ws_col;
	uint16_t ws_xpixel;
	uint16_t ws_ypixel;
};

#endif // __TTY_TTY_H__