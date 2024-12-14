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

#include "console.h"
#include <dev/graphic/fb.h>
#include <libk/serial.h>
#include <libk/type.h>

int pos_x = 0;
int pos_y = -2;
uint32_t fgcolor = 0xffefff;

int console_get_pos_x() { return pos_x; }
int console_get_pos_y() { return pos_y; }

// print string with newline
void console_println(const char *str) {
  // serial_send_number(pos_y, 10);
  while (*str != '\0') {
    fb_put_char(*str++, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
    pos_x += 1;
  }
  pos_y += 1;
  pos_x = 0;
}

void console_print(const char *str, uint64_t len) {
  for (uint64_t i = 0; i < len; i++) {
    if (str[i] == '\n') {
      pos_y += 1;
      pos_x = 0;
      continue;
    }
    fb_put_char(str[i], pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
    pos_x += 1;
  }
}

// convert number to string
char *val_to_str(uint64_t val, int base) {
  if (val == 0) {
    return "0";
  }

  char *str = "0123456789ABCDEF";
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
void console_printf(const char *fmt, ...) {
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
        char *str = __builtin_va_arg(args, char *);
        while (*str != '\0') {
          fb_put_char(*str, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
          str++;
          pos_x += 1;
        }
        break;
      }
      case 'd': {
        int num = __builtin_va_arg(args, int);
        char *str = val_to_str(num, 10);
        while (*str != '\0') {
          fb_put_char(*str++, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
          pos_x += 1;
        }
        break;
      }
      case 'b': {
        int num = __builtin_va_arg(args, int);
        char *str = val_to_str(num, 2);
        while (*str != '\0') {
          fb_put_char(*str++, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
          pos_x += 1;
        }
        break;
      }
      }
      // fmt++;
    } else {
      fb_put_char(*fmt, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
      pos_x += 1;
    }
    fmt++;
  }

  __builtin_va_end(args);
}

void console_vaprintf(const char *fmt, __builtin_va_list args) {
  while (*fmt != '\0') {
    if (*fmt == '\n') {
      pos_y += 1;
      pos_x = 0;
      fmt++;
      continue;
    }
    if (pos_y > fb_get_height()) {
      // fb_scroll_up();
      // pos_y -= 16;
    }
    if (*fmt == '%') {
      fmt++;
      switch (*fmt) {
      case 's': {
        char *str = __builtin_va_arg(args, char *);
        while (*str != '\0') {
          fb_put_char(*str, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
          str++;
          pos_x += 1;
        }
        break;
      }
      case 'd': {
        int num = __builtin_va_arg(args, uint64_t);
        char *str = val_to_str(num, 10);
        while (*str != '\0') {
          fb_put_char(*str++, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
          pos_x += 1;
        }
        break;
      }
      case 'x': {
        uint64_t num = __builtin_va_arg(args, uint64_t);
        char *str = val_to_str(num, 16);
        while (*str != '\0') {
          fb_put_char(*str, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
          pos_x += 1;
          str++;
        }
        break;
      }
      case 'b': {
        int num = __builtin_va_arg(args, uint64_t);
        char *str = val_to_str(num, 2);
        while (*str != '\0') {
          fb_put_char(*str++, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
          pos_x += 1;
        }
        break;
      }
      }
    } else {
      fb_put_char(*fmt, pos_x, pos_y, fgcolor, FB_COLOR_BLACK);
      pos_x += 1;
    }
    fmt++;
  }
}

void console_newline() {
  pos_y += 1;
  pos_x = 0;
}

void console_chfg(uint32_t color) { fgcolor = color; }

void console_add_space(int n) { pos_x += n; }

void console_set_pos(int x, int y) {
  pos_x = x;
  pos_y = y;
}