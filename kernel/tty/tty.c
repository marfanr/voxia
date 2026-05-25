#include "tty.h"
#include "hal/graphic/graphic.h"
#include "init/init.h"
#include "libk/serial.h"
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
#define FONT_SIZE  14

// forward declarations
static void configure_tty(int tty);
static int char_ioctl(vnode_t* vnode, uint32_t req, void* arg);
static long char_write(vnode_t* vnode, void* buf, size_t len, size_t offset);
static void do_scroll(struct tty_internal* priv);

static dentry_ptr   __tty_dentry[VOXIA_TTY_MAX_COUNT] = {0};
static int          __current_tty_active               = 0;
static vops_file_t* __tty_ops                          = 0;

// TODO: will be moved
struct win_size {
	uint16_t ws_row;
	uint16_t ws_col;
	uint16_t ws_xpixel;
	uint16_t ws_ypixel;
};

static void do_scroll(struct tty_internal* priv) {
	vxScroll(FONT_SIZE);
	priv->cursory = priv->rows - 1;
	priv->cursorx = 0;
}

INIT(TTY) {
	__tty_ops        = (vops_file_t*)kalloc(sizeof(vops_file_t));
	__tty_ops->ioctl = char_ioctl;
	__tty_ops->write = char_write;

	for (int i = 0; i < VOXIA_TTY_MAX_COUNT; i++) {
		configure_tty(i);
	}
}

static int char_ioctl(vnode_t* vnode, uint32_t req, void* arg) {
	switch (req) {
	case TIOCGWINSZ: {
		serial2_printf("ioctl: win size request\n");
		struct win_size* ws = (struct win_size*)arg;
		if (!ws)
			return -EBADF;

		auto priv      = (struct tty_internal*)vnode->vnode_private;
		ws->ws_row     = priv ? (uint16_t)priv->rows : 24;
		ws->ws_col     = priv ? (uint16_t)priv->cols : 80;
		ws->ws_xpixel  = (uint16_t)vxGetWidth();
		ws->ws_ypixel  = (uint16_t)vxGetHeight();
		return 0;
	}
	}

	return -ENOTTY;
}

static long char_write(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	UNUSED(offset);

	auto priv = (struct tty_internal*)vnode->vnode_private;
	if (!priv)
		return -ENOENT;

	if (!priv->enable)
		return -ENOENT;

	if (!len)
		return 0;

	auto   tail      = priv->tail;
	size_t available = VOXIA_TTY_INPUT_BUFFER_SIZE - tail;
	if (len > available)
		return -ENOSPC;

	size_t first_chunk = VOXIA_TTY_INPUT_BUFFER_SIZE - tail;

	if (len <= first_chunk) {
		memcopy((uint8_t*)priv->input_buffer + tail, buf, len);
	} else {
		memcopy((uint8_t*)priv->input_buffer + tail, buf, first_chunk);
		memcopy((uint8_t*)priv->input_buffer,
		        (uint8_t*)buf + first_chunk, len - first_chunk);
	}

	priv->tail = (tail + len) & (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);

	if (memchr(buf, '\n', len)) {
		priv->dirty = true;
		vxAddWorkqueueTask((void (*)(void*))tty_check_and_flush, NULL, NULL);
	}

	return (long)len;
}

static void configure_tty(int tty) {
	auto curr_dentry = &__tty_dentry[tty];
	auto path_name   = str_concat(str("/dev/tty"), itoa(tty, 10));
	vxnamei(path_name->c_str, curr_dentry);
	str_release(path_name);

	vnode_ptr_t vnode = create_and_attach_vnode();
	vnode->type       = VNODE_TYPE_CHR;

	(*curr_dentry)->vnode             = vnode;
	(*curr_dentry)->vnode->ops        = __tty_ops;
	(*curr_dentry)->vnode->permission = 660;

	auto dev = create_dev(__tty_ops, DEV_MAJOR_TTY);
	(*curr_dentry)->vnode->device.major  = dev->major;
	(*curr_dentry)->vnode->device.minor  = dev->minor;
	(*curr_dentry)->vnode->mountedhere   = dev;

	auto priv = (struct tty_internal*)kalloc(sizeof(struct tty_internal));
	memset(priv, 0, sizeof(struct tty_internal));
	(*curr_dentry)->vnode->vnode_private = priv;

	priv->enable  = true;
	priv->dirty   = false;
	priv->cols    = screen_cols();
	priv->rows    = screen_rows();
	priv->cursorx = 0;
	priv->cursory = 0;
}

void start_tty() {
	console_set_pos(0, 0);
	clear_screen(0x000000);
}

void change_active_tty(int tty) { __current_tty_active = tty; }

int get_active_tty() { return __current_tty_active; }

dentry_ptr get_active_tty_dentry() {
	return __tty_dentry[__current_tty_active];
}

void tty_check_and_flush() {
	auto dentry = get_active_tty_dentry();
	if (!dentry)
		return;

	auto priv = (struct tty_internal*)dentry->vnode->vnode_private;
	if (!priv || !priv->dirty)
		return;

	LOG2_INFO("TTY", "flushed into screen on tty %d", __current_tty_active);

	priv->dirty = false;

	while (priv->head != priv->tail) {
		char c    = priv->input_buffer[priv->head];
		priv->head = (priv->head + 1) & (VOXIA_TTY_INPUT_BUFFER_SIZE - 1);

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
				vxPutc(' ', (int)priv->cursorx,
				       (int)priv->cursory, 0xFFFFFF, 0x000000);
			}
		} else {
			vxPutc(c, (int)priv->cursorx, (int)priv->cursory,
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

dentry_ptr get_tty_dentry(int tty) { return __tty_dentry[tty]; }