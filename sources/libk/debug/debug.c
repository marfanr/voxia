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

#include "debug.h"
#include <libk/console/console.h>
#include <libk/serial.h>

char *get_debug_level_str(DEBUG_LEVEL level_) {
  switch (level_) {
  case DEBUG_LEVEL_INFO:
    return "INFO";
    break;
  case DEBUG_LEVEL_WARN:
    return "WARN";
    break;
  case DEBUG_LEVEL_ERROR:
    return "ERROR";
    break;
  case DEBUG_LEVEL_DEBUG:
    return "DEBUG";
    break;
  default:
    return "UNKNOWN";
    break;
  }
}

void kernel_debug_impl(const char *file_, uint16_t line_num_,
                       DEBUG_LEVEL level_, const char *message_, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, message_);
  switch (level_) {
  case DEBUG_LEVEL_INFO:
    console_chfg(0xFFFFC0);
    break;
  case DEBUG_LEVEL_DEBUG:
    console_chfg(0x0aA2A5);
    break;
  case DEBUG_LEVEL_ERROR:
    console_chfg(0xDC283A);
    break;
  }
  // console_printf("[%s] at %s:%d|", get_debug_level_str(level_), file_,
  //                line_num_);
  // console_printf("[%s]", get_debug_level_str(level_));
  // console_add_space(1);
  console_vaprintf(message_, args);
  console_newline();
  __builtin_va_end(args);
}

void kernel_assert_impl(const char *file_, uint16_t line_num_) {}