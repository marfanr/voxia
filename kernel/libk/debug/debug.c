#include "debug.h"
#include <libk/console/console.h>
#include <libk/serial.h>

char *
get_debug_level_str(DEBUG_LEVEL level_) {
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

void kernel_debug_impl(const char *file_, uint16_t line_num_, DEBUG_LEVEL level_,
                       const char *message_, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, message_);
    switch (level_) {
        case DEBUG_LEVEL_INFO:
            console_chfg(0xFFFFF0);
            break;
        case DEBUG_LEVEL_DEBUG:
            console_chfg(0x00FE00);
            break;
        case DEBUG_LEVEL_ERROR:
            console_chfg(0xDC283A);
            break;
    }
    // console_printf("[%s] at %s:%d|", get_debug_level_str(level_), file_,
    // line_num_); console_printf("[%s]", get_debug_level_str(level_));
    // console_add_space(1);
    console_vaprintf(message_, args);
    // console_newline ();
    __builtin_va_end(args);
}

void kernel_assert_impl(const char *file_, uint16_t line_num_) {
}