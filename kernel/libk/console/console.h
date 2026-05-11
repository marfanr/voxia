/*
  BSD 3-Clause License

  Copyright (c) 2023, Mohammad Arfan
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __LIBK__CONSOLE__CONSOLE_H_
#define __LIBK__CONSOLE__CONSOLE_H_

#include <type.h>

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

#endif // __LIBK__CONSOLE__CONSOLE_H_