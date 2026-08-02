#ifndef __PTY__PTY_H__
#define __PTY__PTY_H__

#include <type.h>

#include "procc/process.h"
#include "procc/thread.h"
#include "termios.h"

#define PTY_MAX_RING_BUFFER 4096

struct pty_ring {
	uint8_t buf[PTY_MAX_RING_BUFFER];
	size_t head;
	size_t tail;
};

struct win_size {
	uint16_t ws_row;
	uint16_t ws_col;
	uint16_t ws_xpixel;
	uint16_t ws_ypixel;
};

struct internal_pty {
	uint32_t id;
	int locked;
	struct termios termios;
	pid_t foreground;
	pid_t owner_pid;

	struct pty_ring master_to_slave;
	struct pty_ring slave_to_master;

	struct thread* master_waiter;
	struct thread* slave_waiter;

	struct win_size ws;
	uint8_t master_closed;
	uint8_t slave_closed;
	uint8_t eof_pending;
};

#endif //__PTY__PTY_H__