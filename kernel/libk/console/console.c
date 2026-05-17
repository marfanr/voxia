#include <console/console.h>
#include "hal/cpu/spinlock.h"
#include <hal/graphic/graphic.h>
#include <libk/serial.h>
#include <type.h>

static int pos_x = 0;
static int pos_y = 0;
static uint32_t fgcolor = 0xFFEFFF;

static spinlock_t console_lock;

int console_get_pos_x() {
	return pos_x;
}
int console_get_pos_y() {
	return pos_y;
}

// print string with newline
void console_println(const char* str) {
	spin_acquire(&console_lock);
	// serial_send_number(pos_y, 10);
	while (*str != '\0') {
		// putc(*str++, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
		pos_x += 1;
	}
	pos_y += 1;
	pos_x = 0;
	spin_release(&console_lock);
}

void console_print(const char* str, uint64_t len) {
	spin_acquire(&console_lock);
	for (uint64_t i = 0; i < len; i++) {
		if (str[i] == '\n') {
			pos_y += 1;
			pos_x = 0;
			continue;
		}
		// putc(str[i], pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
		pos_x += 1;
	}
	spin_release(&console_lock);
}

// convert number to string
static char* val_to_str(uint64_t val, uint64_t base) {
	if (val == 0) {
		return "0";
	}

	char* str = "0123456789ABCDEF";
	static char buffer[128] = {0};
	int i = 0;
	while (val > 0) {
		buffer[i] = str[val % base];
		val /= base;
		i++;
	}
	buffer[i] = '\0';
	static char buffer2[128] = {0};
	int j = 0;
	for (i = i - 1; i >= 0; i--) {
		buffer2[j] = buffer[i];
		j++;
	}
	buffer2[j] = '\0';
	return &buffer2[0];
}

// print formatted string
void console_printf(const char* fmt, ...) {
	spin_acquire(&console_lock);
	__builtin_va_list args;
	__builtin_va_start(args, fmt);
	while (*fmt != '\0') {
		if (*fmt == '\n') {
			pos_y += 1;
			pos_x = 0;
			fmt++;
			continue;
		}
		if (*fmt == '%') {
			fmt++;
			switch (*fmt) {
			case 's': {
				char* str = __builtin_va_arg(args, char*);
				while (*str != '\0') {
					vxPutc(*str, pos_x, pos_y, fgcolor,
					       BLACK);
					str++;
					pos_x += 1;
				}
				break;
			}
			case 'd': {
				int num = __builtin_va_arg(args, int);
				char* str = val_to_str((uint64_t) num, 10);
				while (*str != '\0') {
					vxPutc(*str++, pos_x, pos_y, fgcolor,
					       BLACK);
					pos_x += 1;
				}
				break;
			}
			case 'b': {
				int num = __builtin_va_arg(args, int);
				char* str = val_to_str((uint64_t) num, 2);
				while (*str != '\0') {
					vxPutc(*str++, pos_x, pos_y, fgcolor,
					       BLACK);
					pos_x += 1;
				}
				break;
			}
			}
			// fmt++;
		} else {
			vxPutc(*fmt, pos_x, pos_y, fgcolor, BLACK);
			pos_x += 1;
		}
		fmt++;
	}

	__builtin_va_end(args);
	spin_release(&console_lock);
}

void console_vaprintf(const char* fmt, __builtin_va_list args) {
	spin_acquire(&console_lock);
	while (*fmt != '\0') {
		if (*fmt == '\n') {
			pos_y += 1;
			pos_x = 0;
			fmt++;
			continue;
		}
		// if (pos_y > fb_get_height())
		// {
		//     // fb_scroll_up();
		//     // pos_y -= 16;
		// }
		if (*fmt == '%') {
			fmt++;
			switch (*fmt) {
			case 's': {
				char* str = __builtin_va_arg(args, char*);
				while (*str != '\0') {
					vxPutc(*str, pos_x, pos_y, fgcolor,
					       BLACK);
					str++;
					pos_x += 1;
				}
				break;
			}
			case 'd': {
				uint64_t num = __builtin_va_arg(args, uint64_t);
				char* str = val_to_str((uint64_t) num, 10);
				while (*str != '\0') {
					vxPutc(*str++, pos_x, pos_y, fgcolor,
					       BLACK);
					pos_x += 1;
				}
				break;
			}
			case 'x': {
				uint64_t num = __builtin_va_arg(args, uint64_t);
				char* str = val_to_str((uint64_t) num, 16);
				while (*str != '\0') {
					vxPutc(*str, pos_x, pos_y, fgcolor,
					       BLACK);
					pos_x += 1;
					str++;
				}
				break;
			}
			case 'b': {
				uint64_t num = __builtin_va_arg(args, uint64_t);
				char* str = val_to_str((uint64_t) num, 2);
				while (*str != '\0') {
					vxPutc(*str++, pos_x, pos_y, fgcolor,
					       BLACK);
					pos_x += 1;
				}
				break;
			}
			}
		} else {
			vxPutc(*fmt, pos_x, pos_y, fgcolor, BLACK);
			pos_x += 1;
		}
		fmt++;
	}
	spin_release(&console_lock);
}

void console_newline() {
	pos_y += 1;
	pos_x = 0;
}

void console_chfg(uint32_t color) {
	fgcolor = color;
}

void console_add_space(int n) {
	pos_x += n;
}

void console_set_pos(int x, int y) {
	pos_x = x;
	pos_y = y;
}