#ifndef __LIBK__CONSOLE__CONSOLE_H_
#define __LIBK__CONSOLE__CONSOLE_H_

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

enum console_color {
	BLACK = 0X000000U,
	BLUE = 0X0000FFU,
	GREEN = 0X00FF00U,
	CYAN = 0X00FFFFU,
	RED = 0XFF0000U,
	MAGENTA = 0XFF00FFU,
	BROWN = 0X8B4513U,
	LIGHT_GREY = 0X808080U,
	DARK_GREY = 0X080808U,
	LIGHT_BLUE = 0X0000FFU,
	LIGHT_GREEN = 0X00FF00U,
	LIGHT_CYAN = 0X00FFFFU,
	LIGHT_RED = 0XFF0000U,
	LIGHT_MAGENTA = 0XFF00FFU,
	YELLOW = 0XFFFF00U,
	WHITE = 0XFFFFFFU
};

void console_println(const char* str);
void console_printf(const char* fmt, ...);
void console_newline();
void console_chfg(uint32_t color);
void console_vaprintf(const char* fmt, __builtin_va_list args);
void console_add_space(int n);
void console_set_pos(int x, int y);
int console_get_pos_x();
int console_get_pos_y();
void console_print(const char* str, uint64_t len);

#ifdef __cplusplus
}
#endif

#endif // __LIBK__CONSOLE__CONSOLE_H_