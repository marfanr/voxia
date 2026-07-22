#include "vxair.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vxair.h>
#include <vxui_text.h>

int vcomp_fd = -1;

int connect_compositor() {
	for (int i = 0; i < 100; i++) {
		vcomp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (vcomp_fd < 0)
			return -1;

		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, "/tmp/vcomp.sock");

		if (connect(vcomp_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
			close(vcomp_fd);
			usleep(1000000);
			continue;
		}

		int fd_flags = fcntl(vcomp_fd, F_GETFD, 0);
		fcntl(vcomp_fd, F_SETFD, fd_flags | FD_CLOEXEC);
		int flags = fcntl(vcomp_fd, F_GETFL, 0);
		fcntl(vcomp_fd, F_SETFL, flags | O_NONBLOCK);
		break;
	}
	return 0;
}

struct message {
	uint32_t type;
	uint32_t len;
	uint32_t data[];
};

enum vcomp_message_type {
	VCOMP_CREATE_WINDOW = 1,
	VCOMP_SUBMIT_RESOURCE,
	VCOMP_DESTROY_WINDOW,
	VCOMP_INPUT_REQUEST,
	VCOMP_INPUT_RESPONSE,
	VCOMP_SET_WINDOW,
	VCOMP_LOG,
	VCOMP_REPORT_KEYMAP,
	VCOMP_KEY_EVENT = 9,
};

static void log_debug(char* fmt, ...);
static char* pts_name = 0;
static int ptmx_fd = -1;

/* ═══════════════════════════════════════════════════════════════════
 * TERMINAL DIMENSIONS
 * ═══════════════════════════════════════════════════════════════════ */
#define WIN_W 500
#define WIN_H 470
#define TERM_PAD_X 4
#define TERM_PAD_Y 4

#define TERM_CELL_W 7
#define TERM_CELL_H 15

#define TERM_COLS ((WIN_W - 2 * TERM_PAD_X) / TERM_CELL_W)
#define TERM_ROWS ((WIN_H - 2 * TERM_PAD_Y) / TERM_CELL_H)

void connect_ptx() {
	ptmx_fd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
	int fd_flags = fcntl(ptmx_fd, F_GETFD, 0);
	fcntl(ptmx_fd, F_SETFD, fd_flags | FD_CLOEXEC);

	int flags = fcntl(ptmx_fd, F_GETFL, 0);
	fcntl(ptmx_fd, F_SETFL, flags | O_NONBLOCK);
	unlockpt(ptmx_fd);
	pts_name = ptsname(ptmx_fd);
	log_debug("pts: %s  cols=%d rows=%d", pts_name, TERM_COLS, TERM_ROWS);
}

/* ═══════════════════════════════════════════════════════════════════
 * LIGHT THEME & COLOR PALETTE
 * ═══════════════════════════════════════════════════════════════════ */
#define THEME_BG_R (247.0f / 255.0f)
#define THEME_BG_G (247.0f / 255.0f)
#define THEME_BG_B (243.0f / 255.0f)

#define THEME_FG_DEFAULT 0x1A1A1AFF
#define THEME_BG_DEFAULT 0xF7F7F3FF

static const uint32_t ansi_colors[16] = {
    0x1A1A1AFF, 0xC0392BFF, 0x27AE60FF, 0xD35400FF, 0x2980B9FF, 0x8E44ADFF, 0x16A085FF, 0x555555FF,
    0x7F8C8DFF, 0xE74C3CFF, 0x2ECC71FF, 0xE67E22FF, 0x3498DBFF, 0x9B59B6FF, 0x1ABC9CFF, 0x1A1A1AFF,
};

static uint32_t xterm256_to_rgba(int idx) {
	if (idx < 0)
		return THEME_FG_DEFAULT;
	if (idx < 16)
		return ansi_colors[idx];
	if (idx < 232) {
		int i = idx - 16;
		int r = i / 36, g = (i / 6) % 6, b = i % 6;
		int rv = r ? r * 40 + 55 : 0;
		int gv = g ? g * 40 + 55 : 0;
		int bv = b ? b * 40 + 55 : 0;
		return ((uint32_t)rv << 24) | ((uint32_t)gv << 16) | ((uint32_t)bv << 8) | 0xFF;
	}
	int gray = 8 + (idx - 232) * 10;
	if (gray > 255)
		gray = 255;
	return ((uint32_t)gray << 24) | ((uint32_t)gray << 16) | ((uint32_t)gray << 8) | 0xFF;
}

/* ═══════════════════════════════════════════════════════════════════
 * TERMINAL STATE & REGION SCROLLING
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct {
	char ch;
	uint32_t fg;
	uint32_t bg;
	uint8_t bold;
} term_cell_t;

static term_cell_t g_cells[64][128];
static term_cell_t g_alt_cells[64][128];
static int g_cur_col = 0;
static int g_cur_row = 0;
static int g_saved_col = 0;
static int g_saved_row = 0;
static int g_in_alt_screen = 0;
static int g_cursor_visible = 1;
static int g_dirty = 1;
static int g_autowrap = 1; /* DECAWM: Auto-wrap mode (default on) */

/* Scrolling Margins (DECSTBM) — Krusial agar judul & status bar nano tidak tergusur */
static int g_scroll_top = 0;
static int g_scroll_bot = TERM_ROWS - 1;

static uint32_t g_attr_fg = THEME_FG_DEFAULT;
static uint32_t g_attr_bg = THEME_BG_DEFAULT;
static uint8_t g_attr_bold = 0;
static uint8_t g_attr_reverse = 0;

static term_cell_t blank_cell(void) {
	uint32_t fg = g_attr_reverse ? g_attr_bg : g_attr_fg;
	uint32_t bg = g_attr_reverse ? g_attr_fg : g_attr_bg;
	return (term_cell_t){' ', fg, bg, g_attr_bold};
}

static void term_fill_cell(int r, int c, char ch) {
	uint32_t fg = g_attr_reverse ? g_attr_bg : g_attr_fg;
	uint32_t bg = g_attr_reverse ? g_attr_fg : g_attr_bg;
	g_cells[r][c] = (term_cell_t){ch, fg, bg, g_attr_bold};
}

static void term_clear_screen() {
	term_cell_t blank = blank_cell();
	for (int r = 0; r < TERM_ROWS; r++)
		for (int c = 0; c < TERM_COLS; c++)
			g_cells[r][c] = blank;
	g_cur_col = 0;
	g_cur_row = 0;
	g_dirty = 1;
}

static void term_clear_eol() {
	term_cell_t blank = blank_cell();
	for (int c = g_cur_col; c < TERM_COLS; c++)
		g_cells[g_cur_row][c] = blank;
	g_dirty = 1;
}

/* Scroll Up hanya di dalam batas region [top .. bot] */
static void term_scroll_region_up(int top, int bot, int n_lines) {
	if (top < 0)
		top = 0;
	if (bot >= TERM_ROWS)
		bot = TERM_ROWS - 1;
	if (top >= bot || n_lines <= 0)
		return;
	term_cell_t blank = blank_cell();

	for (int r = top; r <= bot - n_lines; r++)
		for (int c = 0; c < TERM_COLS; c++)
			g_cells[r][c] = g_cells[r + n_lines][c];
	for (int r = bot - n_lines + 1; r <= bot; r++)
		if (r >= top)
			for (int c = 0; c < TERM_COLS; c++)
				g_cells[r][c] = blank;
	g_dirty = 1;
}

/* Scroll Down hanya di dalam batas region [top .. bot] */
static void term_scroll_region_down(int top, int bot, int n_lines) {
	if (top < 0)
		top = 0;
	if (bot >= TERM_ROWS)
		bot = TERM_ROWS - 1;
	if (top >= bot || n_lines <= 0)
		return;
	term_cell_t blank = blank_cell();

	for (int r = bot; r >= top + n_lines; r--)
		for (int c = 0; c < TERM_COLS; c++)
			g_cells[r][c] = g_cells[r - n_lines][c];
	for (int r = top; r < top + n_lines && r <= bot; r++)
		for (int c = 0; c < TERM_COLS; c++)
			g_cells[r][c] = blank;
	g_dirty = 1;
}

static void term_newline() {
	g_cur_col = 0;
	if (g_cur_row == g_scroll_bot) {
		term_scroll_region_up(g_scroll_top, g_scroll_bot, 1);
	} else if (g_cur_row < TERM_ROWS - 1) {
		g_cur_row++;
	}
	g_dirty = 1;
}

static void term_put_char(char ch) {
	if (g_cur_col >= TERM_COLS) {
		if (g_autowrap)
			term_newline();
		else
			g_cur_col = TERM_COLS - 1; /* Clamp at last column */
	}
	term_fill_cell(g_cur_row, g_cur_col, ch);
	g_cur_col++;
	g_dirty = 1;
}

static void term_switch_alt_screen(int enable) {
	if (enable && !g_in_alt_screen) {
		memcpy(g_alt_cells, g_cells, sizeof(g_cells));
		g_saved_col = g_cur_col;
		g_saved_row = g_cur_row;
		g_in_alt_screen = 1;
		term_clear_screen();
	} else if (!enable && g_in_alt_screen) {
		memcpy(g_cells, g_alt_cells, sizeof(g_cells));
		g_cur_col = g_saved_col;
		g_cur_row = g_saved_row;
		g_in_alt_screen = 0;
		g_dirty = 1;
	}
}

/* ═══════════════════════════════════════════════════════════════════
 * ANSI ESCAPE PARSER
 * ═══════════════════════════════════════════════════════════════════ */
typedef enum { ANSI_NORMAL = 0, ANSI_ESC, ANSI_CSI, ANSI_OSC, ANSI_CHARSET } ansi_state_t;
static ansi_state_t g_ansi_state = ANSI_NORMAL;

#define ANSI_MAX_PARAMS 16
static int g_csi_params[ANSI_MAX_PARAMS];
static int g_csi_nparam = 0;
static char g_csi_inter = 0;
static int g_csi_private = 0;

static void ansi_apply_sgr(int* p, int n) {
	if (n == 0) {
		g_attr_fg = THEME_FG_DEFAULT;
		g_attr_bg = THEME_BG_DEFAULT;
		g_attr_bold = 0;
		g_attr_reverse = 0;
		return;
	}
	int i = 0;
	while (i < n) {
		int v = p[i];
		if (v == 0) {
			g_attr_fg = THEME_FG_DEFAULT;
			g_attr_bg = THEME_BG_DEFAULT;
			g_attr_bold = 0;
			g_attr_reverse = 0;
		} else if (v == 1) {
			g_attr_bold = 1;
		} else if (v == 2) {
			g_attr_bold = 0; /* dim: treat as non-bold */
		} else if (v == 3 || v == 23) {
			/* italic / italic-off: silently accept */
		} else if (v == 4 || v == 21 || v == 24) {
			/* underline / double-underline / underline-off: silently accept */
		} else if (v == 22) {
			g_attr_bold = 0;
		} else if (v == 7) {
			g_attr_reverse = 1;
		} else if (v == 27) {
			g_attr_reverse = 0;
		} else if (v >= 30 && v <= 37) {
			g_attr_fg = ansi_colors[v - 30 + (g_attr_bold ? 8 : 0)];
		} else if (v == 38 && i + 4 < n && p[i + 1] == 2) {
			g_attr_fg = (p[i + 2] << 24) | (p[i + 3] << 16) | (p[i + 4] << 8) | 0xFF;
			i += 4;
		} else if (v == 38 && i + 2 < n && p[i + 1] == 5) {
			g_attr_fg = xterm256_to_rgba(p[i + 2]);
			i += 2;
		} else if (v == 39) {
			g_attr_fg = THEME_FG_DEFAULT;
		} else if (v >= 40 && v <= 47) {
			g_attr_bg = ansi_colors[v - 40];
		} else if (v == 48 && i + 4 < n && p[i + 1] == 2) {
			g_attr_bg = (p[i + 2] << 24) | (p[i + 3] << 16) | (p[i + 4] << 8) | 0xFF;
			i += 4;
		} else if (v == 48 && i + 2 < n && p[i + 1] == 5) {
			g_attr_bg = xterm256_to_rgba(p[i + 2]);
			i += 2;
		} else if (v == 49) {
			g_attr_bg = THEME_BG_DEFAULT;
		} else if (v >= 90 && v <= 97) {
			g_attr_fg = ansi_colors[v - 90 + 8];
		} else if (v >= 100 && v <= 107) {
			g_attr_bg = ansi_colors[v - 100 + 8];
		}
		i++;
	}
}

static void ansi_dispatch_csi(char f) {
	int* p = g_csi_params;
	int n = g_csi_nparam;
	if (n == 0) {
		p[0] = 0;
		n = 1;
	}

	if (g_csi_private) {
		for (int i = 0; i < n; i++) {
			int mode = p[i];
			if (mode == 47 || mode == 1047 || mode == 1049) {
				term_switch_alt_screen(f == 'h' || f == 'H');
			} else if (mode == 25) {
				g_cursor_visible = (f == 'h' || f == 'H');
			} else if (mode == 7) {
				g_autowrap = (f == 'h' || f == 'H');
			} else if (mode == 12 || mode == 2004 || mode == 1) {
				/* mode 12: cursor blink (ignored, we have our own blink) */
				/* mode 2004: bracketed paste (silently accepted) */
				/* mode 1: cursor keys mode (silently accepted) */
			}
		}
		g_dirty = 1;
		return;
	}

	switch (f) {
	case 'A':
		g_cur_row -= (p[0] ? p[0] : 1);
		if (g_cur_row < 0)
			g_cur_row = 0;
		break;
	case 'B':
		g_cur_row += (p[0] ? p[0] : 1);
		if (g_cur_row >= TERM_ROWS)
			g_cur_row = TERM_ROWS - 1;
		break;
	case 'C':
		g_cur_col += (p[0] ? p[0] : 1);
		if (g_cur_col >= TERM_COLS)
			g_cur_col = TERM_COLS - 1;
		break;
	case 'D':
		g_cur_col -= (p[0] ? p[0] : 1);
		if (g_cur_col < 0)
			g_cur_col = 0;
		break;
	case 'E':
		g_cur_row += (p[0] ? p[0] : 1);
		if (g_cur_row >= TERM_ROWS)
			g_cur_row = TERM_ROWS - 1;
		g_cur_col = 0;
		break;
	case 'F':
		g_cur_row -= (p[0] ? p[0] : 1);
		if (g_cur_row < 0)
			g_cur_row = 0;
		g_cur_col = 0;
		break;
	case 'G':
		g_cur_col = (p[0] > 0 ? p[0] - 1 : 0);
		if (g_cur_col >= TERM_COLS)
			g_cur_col = TERM_COLS - 1;
		break;
	case 'H':
	case 'f': {
		int r = (n >= 1 && p[0] > 0) ? p[0] - 1 : 0;
		int c = (n >= 2 && p[1] > 0) ? p[1] - 1 : 0;
		g_cur_row = r < TERM_ROWS ? r : TERM_ROWS - 1;
		g_cur_col = c < TERM_COLS ? c : TERM_COLS - 1;
		break;
	}
	case 'J': {
		term_cell_t blank = blank_cell();
		if (p[0] == 0) {
			term_clear_eol();
			for (int r = g_cur_row + 1; r < TERM_ROWS; r++)
				for (int c = 0; c < TERM_COLS; c++)
					g_cells[r][c] = blank;
		} else if (p[0] == 1) {
			for (int r = 0; r < g_cur_row; r++)
				for (int c = 0; c < TERM_COLS; c++)
					g_cells[r][c] = blank;
			for (int c = 0; c <= g_cur_col; c++)
				g_cells[g_cur_row][c] = blank;
		} else if (p[0] == 2 || p[0] == 3) {
			term_clear_screen();
		}
		break;
	}
	case 'K': {
		term_cell_t blank = blank_cell();
		if (p[0] == 0)
			term_clear_eol();
		else if (p[0] == 1) {
			for (int c = 0; c <= g_cur_col; c++)
				g_cells[g_cur_row][c] = blank;
		} else if (p[0] == 2) {
			for (int c = 0; c < TERM_COLS; c++)
				g_cells[g_cur_row][c] = blank;
		}
		break;
	}
	/* Insert Line — dibatasi hanya pada scrolling region agar tidak merusak menu bawah */
	case 'L': {
		int n_lines = (p[0] > 0) ? p[0] : 1;
		int bot = (g_cur_row <= g_scroll_bot) ? g_scroll_bot : TERM_ROWS - 1;
		term_cell_t blank = blank_cell();
		for (int r = bot; r >= g_cur_row + n_lines; r--)
			for (int c = 0; c < TERM_COLS; c++)
				g_cells[r][c] = g_cells[r - n_lines][c];
		for (int r = g_cur_row; r < g_cur_row + n_lines && r <= bot; r++)
			for (int c = 0; c < TERM_COLS; c++)
				g_cells[r][c] = blank;
		break;
	}
	/* Delete Line — dibatasi hanya pada scrolling region */
	case 'M': {
		int n_lines = (p[0] > 0) ? p[0] : 1;
		int bot = (g_cur_row <= g_scroll_bot) ? g_scroll_bot : TERM_ROWS - 1;
		term_cell_t blank = blank_cell();
		for (int r = g_cur_row; r <= bot - n_lines; r++)
			for (int c = 0; c < TERM_COLS; c++)
				g_cells[r][c] = g_cells[r + n_lines][c];
		for (int r = bot - n_lines + 1; r <= bot; r++)
			if (r >= g_cur_row)
				for (int c = 0; c < TERM_COLS; c++)
					g_cells[r][c] = blank;
		break;
	}
	case '@': { /* Insert Character */
		int n_chars = (p[0] > 0) ? p[0] : 1;
		term_cell_t blank = blank_cell();
		for (int c = TERM_COLS - 1; c >= g_cur_col + n_chars; c--)
			g_cells[g_cur_row][c] = g_cells[g_cur_row][c - n_chars];
		for (int c = g_cur_col; c < g_cur_col + n_chars && c < TERM_COLS; c++)
			g_cells[g_cur_row][c] = blank;
		break;
	}
	case 'P': { /* Delete Character */
		int n_chars = (p[0] > 0) ? p[0] : 1;
		term_cell_t blank = blank_cell();
		for (int c = g_cur_col; c < TERM_COLS - n_chars; c++)
			g_cells[g_cur_row][c] = g_cells[g_cur_row][c + n_chars];
		for (int c = TERM_COLS - n_chars; c < TERM_COLS; c++)
			if (c >= 0)
				g_cells[g_cur_row][c] = blank;
		break;
	}
	case 'X': { /* Erase Character */
		int n_chars = (p[0] > 0) ? p[0] : 1;
		term_cell_t blank = blank_cell();
		for (int c = g_cur_col; c < g_cur_col + n_chars && c < TERM_COLS; c++)
			g_cells[g_cur_row][c] = blank;
		break;
	}
	case 'd': {
		int r = (n >= 1 && p[0] > 0) ? p[0] - 1 : 0;
		g_cur_row = r < TERM_ROWS ? r : TERM_ROWS - 1;
		break;
	}
	/* Set Scrolling Margins (DECSTBM) */
	case 'r': {
		int top = (n >= 1 && p[0] > 0) ? p[0] - 1 : 0;
		int bot = (n >= 2 && p[1] > 0) ? p[1] - 1 : TERM_ROWS - 1;
		if (top < bot && bot < TERM_ROWS) {
			g_scroll_top = top;
			g_scroll_bot = bot;
			g_cur_row = 0;
			g_cur_col = 0;
		}
		break;
	}
	case 'S': /* Scroll Up dalam Region */
		term_scroll_region_up(g_scroll_top, g_scroll_bot, (p[0] > 0) ? p[0] : 1);
		break;
	case 'T': /* Scroll Down dalam Region */
		term_scroll_region_down(g_scroll_top, g_scroll_bot, (p[0] > 0) ? p[0] : 1);
		break;
	case 's':
		g_saved_col = g_cur_col;
		g_saved_row = g_cur_row;
		break;
	case 'u':
		g_cur_col = g_saved_col;
		g_cur_row = g_saved_row;
		break;
	/* Balikan untuk Device Attributes & DSR (Mencegah timeout pada ncurses/bash) */
	case 'c': {
		if (ptmx_fd >= 0) {
			if (g_csi_inter == '>') {
				const char* rsp = "\x1b[>1;95;0c";
				write(ptmx_fd, rsp, strlen(rsp));
			} else {
				const char* rsp = "\x1b[?62;c";
				write(ptmx_fd, rsp, strlen(rsp));
			}
		}
		break;
	}
	case 'n': {
		if (p[0] == 6 && ptmx_fd >= 0) {
			char rsp[32];
			int r = (g_cur_row + 1 > TERM_ROWS) ? TERM_ROWS : g_cur_row + 1;
			int c = (g_cur_col + 1 > TERM_COLS) ? TERM_COLS : g_cur_col + 1;
			int len = snprintf(rsp, sizeof(rsp), "\x1b[%d;%dR", r, c);
			write(ptmx_fd, rsp, len);
		}
		break;
	}
	case 'm':
		ansi_apply_sgr(g_csi_params, g_csi_nparam);
		break;
	default:
		break;
	}
	g_dirty = 1;
}

static void term_feed(const char* buf, int len) {
	for (int i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)buf[i];
		switch (g_ansi_state) {
		case ANSI_NORMAL:
			if (ch == 0x1B) {
				g_ansi_state = ANSI_ESC;
			} else if (ch == '\n') {
				term_newline();
			} else if (ch == '\r') {
				g_cur_col = 0;
				g_dirty = 1;
			} else if (ch == '\b') {
				if (g_cur_col > 0) {
					g_cur_col--;
					g_dirty = 1;
				}
			} else if (ch == '\t') {
				int nx = ((g_cur_col / 8) + 1) * 8;
				if (nx >= TERM_COLS)
					nx = TERM_COLS - 1;
				while (g_cur_col < nx)
					term_put_char(' ');
			} else if (ch >= 32 && ch < 127) {
				term_put_char((char)ch);
			}
			break;
		case ANSI_ESC:
			if (ch == '[') {
				g_ansi_state = ANSI_CSI;
				g_csi_nparam = 0;
				g_csi_inter = 0;
				g_csi_private = 0;
				memset(g_csi_params, 0, sizeof(g_csi_params));
			} else if (ch == ']') {
				g_ansi_state = ANSI_OSC;
			} else if (ch == '(' || ch == ')' || ch == '*' || ch == '+') {
				g_ansi_state = ANSI_CHARSET;
			} else if (ch == '7') {
				g_saved_col = g_cur_col;
				g_saved_row = g_cur_row;
				g_ansi_state = ANSI_NORMAL;
			} else if (ch == '8') {
				g_cur_col = g_saved_col;
				g_cur_row = g_saved_row;
				g_ansi_state = ANSI_NORMAL;
			} else if (ch == 'M') { /* Reverse Index (RI) di dalam batas scrolling region */
				if (g_cur_row == g_scroll_top) {
					term_scroll_region_down(g_scroll_top, g_scroll_bot, 1);
				} else if (g_cur_row > 0) {
					g_cur_row--;
				}
				g_dirty = 1;
				g_ansi_state = ANSI_NORMAL;
			} else if (ch == 'D') { /* Index (IND) */
				if (g_cur_row == g_scroll_bot) {
					term_scroll_region_up(g_scroll_top, g_scroll_bot, 1);
				} else if (g_cur_row < TERM_ROWS - 1) {
					g_cur_row++;
				}
				g_dirty = 1;
				g_ansi_state = ANSI_NORMAL;
			} else if (ch == 'E') { /* Next Line (NEL) */
				g_cur_col = 0;
				if (g_cur_row == g_scroll_bot) {
					term_scroll_region_up(g_scroll_top, g_scroll_bot, 1);
				} else if (g_cur_row < TERM_ROWS - 1) {
					g_cur_row++;
				}
				g_dirty = 1;
				g_ansi_state = ANSI_NORMAL;
			} else {
				g_ansi_state = ANSI_NORMAL;
			}
			break;
		case ANSI_OSC:
			if (ch == '\007') {
				g_ansi_state = ANSI_NORMAL;
			} else if (ch == 0x1B) {
				/* Check for ST (ESC \) terminator */
				if (i + 1 < len && (unsigned char)buf[i + 1] == '\\') {
					i++; /* Consume the backslash */
					g_ansi_state = ANSI_NORMAL;
				} else {
					g_ansi_state = ANSI_ESC;
				}
			}
			break;
		case ANSI_CHARSET:
			g_ansi_state = ANSI_NORMAL;
			break;
		case ANSI_CSI:
			if (ch >= '0' && ch <= '9') {
				if (g_csi_nparam == 0)
					g_csi_nparam = 1;
				g_csi_params[g_csi_nparam - 1] = g_csi_params[g_csi_nparam - 1] * 10 + (ch - '0');
			} else if (ch == ';') {
				if (g_csi_nparam == 0)
					g_csi_nparam = 1;
				if (g_csi_nparam < ANSI_MAX_PARAMS)
					g_csi_nparam++;
			} else if (ch == '?') {
				g_csi_private = 1;
			} else if (ch >= 0x20 && ch <= 0x2F) {
				g_csi_inter = (char)ch;
			} else if (ch >= 0x40 && ch <= 0x7E) {
				ansi_dispatch_csi((char)ch);
				g_ansi_state = ANSI_NORMAL;
				g_csi_private = 0;
			} else {
				g_ansi_state = ANSI_NORMAL;
				g_csi_private = 0;
			}
			break;
		}
	}
}

/* ═══════════════════════════════════════════════════════════════════
 * vcomp IPC & VBO POOL
 * ═══════════════════════════════════════════════════════════════════ */
static void handle_vcomp_messages(const char* buf, int n) {
	int offset = 0;
	while (offset + (int)sizeof(struct message) <= n) {
		const struct message* msg = (const struct message*)(buf + offset);
		int total = (int)(sizeof(struct message) + msg->len);
		if (offset + total > n)
			break;
		if (msg->type == VCOMP_KEY_EVENT && msg->len > 0 && ptmx_fd >= 0) {
			write(ptmx_fd, (const char*)msg->data, msg->len);
		}
		offset += total;
	}
}

#define MAX_ROWS 64
#define MAX_RUNS 128
static vxair_buffer_t* g_vbo_pool[MAX_ROWS][MAX_RUNS];
static vxair_buffer_t* g_cur_vbo = NULL;

/* ═══════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════ */
int main(void) {
	connect_compositor();
	connect_ptx();

	{
		struct message init;
		init.type = VCOMP_CREATE_WINDOW;
		init.len = 0;
		write(vcomp_fd, &init, sizeof(init));
	}

	vxair_device_t* dev = vxair_device_create(VXAIR_BACKEND_VIRGL);
	vxair_context_t* ctx = vxair_context_create(dev, WIN_W, WIN_H);
	vxair_switch_context(dev, ctx);

	vxui_font_t* font = vxui_font_create(dev, "/usr/shared/fonts/JetBrainsMono-Regular.ttf", 13, 32, 126, FONT_STYLE_REGULAR);
	vxui_font_t* font_bold = vxui_font_create(dev, "/usr/shared/fonts/JetBrainsMono-SemiBold.ttf", 13, 32, 126, FONT_STYLE_BOLD);

	/* HACK: Modify the space character in the font to have a width and height equal to TERM_CELL_W and TERM_CELL_H,
	   so that we can use it to draw full-cell background blocks. */
	struct vxui_font_impl {
		void* tex;
		struct {
			float u0, v0, u1, v1;
			int width, height;
			int bearing_x, bearing_y;
			int advance;
			int glyph_index;
		} glyphs[128];
		int atlas_w, atlas_h;
		float ascender;
		void* face;
	};
	struct vxui_font_impl* fi = (struct vxui_font_impl*)font;
	fi->glyphs[' '].width = TERM_CELL_W;
	fi->glyphs[' '].height = TERM_CELL_H;
	fi->glyphs[' '].bearing_x = 0;
	fi->glyphs[' '].bearing_y = (int)fi->ascender;

	struct vxui_font_impl* fbi = (struct vxui_font_impl*)font_bold;
	fbi->glyphs[' '].width = TERM_CELL_W;
	fbi->glyphs[' '].height = TERM_CELL_H;
	fbi->glyphs[' '].bearing_x = 0;
	fbi->glyphs[' '].bearing_y = (int)fbi->ascender;
	vxui_text_renderer_t* text_renderer = vxui_text_renderer_create(dev, WIN_W, WIN_H);

	uint32_t alpha_blend_id = vxair_create_alpha_blend(ctx);
	vxair_bind_blend(ctx, alpha_blend_id);
	memset(g_vbo_pool, 0, sizeof(g_vbo_pool));

	// submit resource
	{
		char buf[sizeof(struct message) + sizeof(uint32_t)];
		struct message* msg = (struct message*)buf;
		msg->type = VCOMP_SUBMIT_RESOURCE;
		msg->len = sizeof(uint32_t);
		msg->data[0] = vxair_context_get_scanout_id(ctx);
		write(vcomp_fd, buf, sizeof(buf));
	}

	term_clear_screen();

	/* Spawn bash */
	{
		fflush(stdout);
		int pid = fork();
		if (pid == 0) {
			setsid();

			if (pts_name) {
				int slave_fd = open(pts_name, O_RDWR);
				if (slave_fd >= 0) {
					/* Konfigurasi sebagai Controlling Terminal (CTTY) */
					ioctl(slave_fd, TIOCSCTTY, 0);

					struct winsize ws;
					ws.ws_col = (unsigned short)TERM_COLS;
					ws.ws_row = (unsigned short)TERM_ROWS;
					ws.ws_xpixel = WIN_W;
					ws.ws_ypixel = WIN_H;
					ioctl(slave_fd, TIOCSWINSZ, &ws);

					dup2(slave_fd, 0);
					dup2(slave_fd, 1);
					dup2(slave_fd, 2);
					if (slave_fd > 2)
						close(slave_fd);
				} else {
					log_debug("vxterm bash spawn: failed to open slave_fd! pts_name=%s", pts_name);
				}
			}

			char* envp[] = {"TERM=xterm-256color",
			                "COLORTERM=truecolor",
			                "HOME=/root",
			                "PATH=/usr/bin:/bin:/sbin:/usr/sbin",
			                "SHELL=/bin/bash",
			                "CLICOLOR=1",
			                "FORCE_COLOR=1",
			                "PS1=\\[\\e[1;32m\\]root@voxia\\[\\e[0m\\]:\\[\\e[1;34m\\]\\w\\[\\e[0m\\]# ",
			                "TERMINFO=/usr/share/terminfo",
			                NULL};
			/* Hapus flag -l agar tidak menimpa PS1 via /etc/profile, cukup interaktif -i */
			char* args[] = {"/usr/bin/bash", "-i", NULL};
			int err = execve("/usr/bin/bash", args, envp);
			log_debug("vxterm bash spawn: execve failed! err=%d", err);
			while (1) {
			}
		} else {
			log_debug("bash pid=%d cols=%d rows=%d", pid, TERM_COLS, TERM_ROWS);
		}
	}

	int blink_tick = 0;
	int cursor_blink_on = 1;

	while (1) {
		{
			char vcomp_buf[512];
			int n = read(vcomp_fd, vcomp_buf, sizeof(vcomp_buf) - 1);
			if (n > 0)
				handle_vcomp_messages(vcomp_buf, n);
		}

		{
			char ptm_buf[4096];
			int n = read(ptmx_fd, ptm_buf, sizeof(ptm_buf) - 1);
			if (n > 0)
				term_feed(ptm_buf, n);
		}

		blink_tick++;
		if (blink_tick >= 30) {
			blink_tick = 0;
			cursor_blink_on = !cursor_blink_on;
			g_dirty = 1;
		}

		if (g_dirty) {
			vxair_cmd_set_viewport(ctx, 0.0f, 0.0f, (float)WIN_W, (float)WIN_H, 0.0f, 0.0f);
			vxair_cmd_clear(ctx, THEME_BG_R, THEME_BG_G, THEME_BG_B, 1.0f);

			for (int r = 0; r < TERM_ROWS && r < MAX_ROWS; r++) {
				float y = (float)(TERM_PAD_Y + r * TERM_CELL_H);
				int run_idx = 0;
				int c = 0;

				while (c < TERM_COLS && run_idx < MAX_RUNS) {
					/* Skip default blank cells (no text, default colors) */
					if ((g_cells[r][c].ch == ' ' || g_cells[r][c].ch == '\0') && g_cells[r][c].fg == THEME_FG_DEFAULT &&
					    g_cells[r][c].bg == THEME_BG_DEFAULT) {
						c++;
						continue;
					}

					uint32_t run_fg = g_cells[r][c].fg;
					uint32_t run_bg = g_cells[r][c].bg;
					uint8_t run_bold = g_cells[r][c].bold;
					int run_start_col = c;
					char run_str[MAX_RUNS + 1];
					int run_len = 0;

					/* Collect a run of cells with the same attributes */
					while (c < TERM_COLS && g_cells[r][c].fg == run_fg && g_cells[r][c].bg == run_bg && g_cells[r][c].bold == run_bold &&
					       run_len < (int)sizeof(run_str) - 1) {
						char ch = g_cells[r][c].ch;
						if (ch < 32 || ch > 126)
							ch = ' ';
						run_str[run_len++] = ch;
						c++;
					}
					run_str[run_len] = '\0';

					/* Use fixed cell-width positioning — no drift */
					float run_x = (float)(TERM_PAD_X + run_start_col * TERM_CELL_W);

					/* Determine background paint */
					uint32_t bg_paint = (run_bg == THEME_BG_DEFAULT) ? (run_bg & 0xFFFFFF00) : run_bg;

					/* Strip trailing spaces for rendering */
					int text_end = run_len;
					while (text_end > 0 && run_str[text_end - 1] == ' ')
						text_end--;

					if (run_bg != THEME_BG_DEFAULT && run_len > 0) {
						/* Background Pass: Draw spaces which are modified to be full-cell blocks */
						char bg_str[TERM_COLS + 1];
						memset(bg_str, ' ', run_len);
						bg_str[run_len] = '\0';

						vxui_text_desc_t bg_opts = {
						    .scale = 1.0f,
						    .color = 0, /* transparent text */
						    .bg_color = run_bg,
						    .align = VXUI_ALIGN_LEFT,
						    .fixed_advance = TERM_CELL_W,
						};
						vxui_draw_text(text_renderer, dev, ctx, font, bg_str, run_x, y, &bg_opts, &g_vbo_pool[r][run_idx]);
						run_idx++;
					}

					if (text_end > 0) {
						run_str[text_end] = '\0';
						vxui_text_desc_t opts = {
						    .scale = 1.0f,
						    .color = run_fg,
						    .bg_color = (run_bg != THEME_BG_DEFAULT) ? (run_bg & 0xFFFFFF00) : (THEME_BG_DEFAULT & 0xFFFFFF00),
						    .align = VXUI_ALIGN_LEFT,
						    .fixed_advance = TERM_CELL_W,
						};
						vxui_draw_text(text_renderer, dev, ctx, run_bold ? font_bold : font, run_str, run_x, y, &opts,
						               &g_vbo_pool[r][run_idx]);
						run_idx++;
					}
				}
			}

			/* Blinking cursor */
			if (g_cursor_visible && cursor_blink_on && g_cur_row < MAX_ROWS) {
				float cx = (float)(TERM_PAD_X + g_cur_col * TERM_CELL_W);

				char cblk[2] = {'_', '\0'};
				float cy = (float)(TERM_PAD_Y + g_cur_row * TERM_CELL_H);
				vxui_text_desc_t opts = {
				    .scale = 1.0f,
				    .color = 0x2980B9FF,
				    .bg_color = THEME_BG_DEFAULT & 0xFFFFFF00,
				    .align = VXUI_ALIGN_LEFT,
				};
				vxui_draw_text(text_renderer, dev, ctx, font, cblk, cx, cy, &opts, &g_cur_vbo);
			}

			vxair_submit_and_present(ctx);
			g_dirty = 0;
		}

		usleep(16000);
	}

	return 0;
}

static void log_debug(char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int len = vsnprintf(NULL, 0, fmt, ap);
	char* buf = malloc(len + 1);
	vsnprintf(buf, len + 1, fmt, ap2);
	va_end(ap2);
	va_end(ap);

	size_t msg_size = sizeof(struct message) + len + 1;
	struct message* m = (struct message*)malloc(msg_size);
	m->type = VCOMP_LOG;
	m->len = len + 1;
	memcpy((void*)m->data, buf, len + 1);
	write(vcomp_fd, m, msg_size);
	free(m);
	free(buf);
}
