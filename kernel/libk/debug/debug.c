#include "debug.h"
#include "type.h"
#include <console/console.h>
#include <libk/serial.h>

extern void parse_multicore(__builtin_va_list args, const char* fmt);

static char* get_debug_level_str(DEBUG_LEVEL level_) {
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
	case DEBUG_LEVEL_OK:
		return "OK";
		break;
	default:
		return "UNKNOWN";
		break;
	}
}

void kernel_debug_impl(const char* file_, uint16_t line_num_,
		       DEBUG_LEVEL level_, const char* message_, ...) {
	UNUSED(file_);
	UNUSED(line_num_);
	__builtin_va_list args;
	__builtin_va_start(args, message_);
	console_printf("[");
	switch (level_) {
	case DEBUG_LEVEL_INFO:
		console_chfg(0x2CC6DE);
		break;
	case DEBUG_LEVEL_DEBUG:
		console_chfg(0x00FE00);
		break;
	case DEBUG_LEVEL_ERROR:
		console_chfg(0xED2641);
		break;
	case DEBUG_LEVEL_OK:
		console_chfg(0x2CDE44);
		break;
	case DEBUG_LEVEL_WARN:
		console_chfg(0xFFDE44);
		break;
	}
	console_printf("%s", get_debug_level_str(level_));
	console_chfg(0xFFFFF0);
	console_printf("]");
	console_add_space(1);
	console_vaprintf(message_, args);
	__builtin_va_end(args);
}

// void kernel_assert_impl(const char* file_, uint16_t line_num_) {
// }