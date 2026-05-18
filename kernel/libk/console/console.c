#include <console/console.h>
#include <spinlock.h>
#include <hal/graphic/graphic.h>
#include <libk/serial.h>
#include <type.h>

#define FONT_SIZE 16 /* harus sama dengan di graphic.c */

static int pos_x = 0;
static int pos_y = 0;
static uint32_t fgcolor = 0xFFFFFFFF;

static spinlock_t console_lock;

/* ── helper internal ─────────────────────────────────────────────────────── */

static int screen_cols(void) {
	return vxGetWidth() / FONT_SIZE;
}
static int screen_rows(void) {
	return vxGetHeight() / FONT_SIZE;
}

/* scroll layar ke atas 1 baris — blok copy pixel */
static void do_scroll(void) {
	vxScroll(FONT_SIZE); /* implementasi di graphic.c — lihat bawah */
	pos_y = screen_rows() - 1;
	pos_x = 0;
}

static void advance_cursor(void) {
	pos_x++;
	if (pos_x >= screen_cols()) {
		pos_x = 0;
		pos_y++;
	}
	if (pos_y >= screen_rows())
		do_scroll();
}

static void put_char(char c) {
	if (c == '\n') {
		pos_x = 0;
		pos_y++;
		if (pos_y >= screen_rows())
			do_scroll();
		return;
	}
	if (c == '\r') {
		pos_x = 0;
		return;
	}
	if (c == '\t') {
		int next = (pos_x + 8) & ~7;
		while (pos_x < next)
			advance_cursor();
		return;
	}
	vxPutc(c, pos_x, pos_y, fgcolor, BLACK);
	advance_cursor();
}

/* ── public API ──────────────────────────────────────────────────────────── */

int console_get_pos_x(void) {
	return pos_x;
}
int console_get_pos_y(void) {
	return pos_y;
}

void console_println(const char* str) {
	spin_acquire(&console_lock);
	while (*str)
		put_char(*str++);
	put_char('\n');
	spin_release(&console_lock);
}

void console_print(const char* str, uint64_t len) {
	spin_acquire(&console_lock);
	for (uint64_t i = 0; i < len; i++)
		put_char(str[i]);
	spin_release(&console_lock);
}

static char* val_to_str(uint64_t val, uint64_t base) {
	if (val == 0)
		return "0";
	static char buf[256], out[256];
	int i = 0, j = 0;
	const char* digits = "0123456789ABCDEF";
	while (val > 0) {
		buf[i++] = digits[val % base];
		val /= base;
	}
	buf[i] = '\0';
	for (i = i - 1; i >= 0; i--)
		out[j++] = buf[i];
	out[j] = '\0';
	return out;
}

static void vprintf_internal(const char* fmt, __builtin_va_list args) {
	while (*fmt) {
		if (*fmt == '%') {
			fmt++;
			switch (*fmt) {
			case 's': {
				char* s = __builtin_va_arg(args, char*);
				if (!s)
					s = "(null)";
				while (*s)
					put_char(*s++);
				break;
			}
			case 'd': {
				int64_t n = __builtin_va_arg(args, int64_t);
				if (n < 0) {
					put_char('-');
					n = -n;
				}
				char* s = val_to_str((uint64_t) n, 10);
				while (*s)
					put_char(*s++);
				break;
			}
			case 'u': {
				char* s = val_to_str(
					__builtin_va_arg(args, uint64_t), 10);
				while (*s)
					put_char(*s++);
				break;
			}
			case 'x': {
				char* s = val_to_str(
					__builtin_va_arg(args, uint64_t), 16);
				while (*s)
					put_char(*s++);
				break;
			}
			case 'b': {
				char* s = val_to_str(
					__builtin_va_arg(args, uint64_t), 2);
				while (*s)
					put_char(*s++);
				break;
			}
			case 'c':
				put_char((char) __builtin_va_arg(args, int));
				break;
			case '%':
				put_char('%');
				break;
			default:
				put_char('%');
				put_char(*fmt);
				break;
			}
		} else {
			put_char(*fmt);
		}
		fmt++;
	}
}

void console_printf(const char* fmt, ...) {
	spin_acquire(&console_lock);
	__builtin_va_list args;
	__builtin_va_start(args, fmt);
	vprintf_internal(fmt, args);
	__builtin_va_end(args);
	spin_release(&console_lock);
}

void console_vaprintf(const char* fmt, __builtin_va_list args) {
	spin_acquire(&console_lock);
	vprintf_internal(fmt, args);
	spin_release(&console_lock);
}

void console_newline(void) {
	spin_acquire(&console_lock);
	put_char('\n');
	spin_release(&console_lock);
}
void console_chfg(uint32_t color) {
	fgcolor = color;
}
void console_add_space(int n) {
	spin_acquire(&console_lock);
	while (n--)
		put_char(' ');
	spin_release(&console_lock);
}
void console_set_pos(int x, int y) {
	pos_x = x;
	pos_y = y;
}