#ifndef __LIBK__OCT2BIN_H__
#define __LIBK__OCT2BIN_H__

#include <type.h>

__attribute__((noinline)) static uint64_t
oct2bin(unsigned char* str, size_t len) {
	uint64_t value = 0;

	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char) str[i];

		if (c == '\0' || c == ' ')
			break;

		if (c < '0' || c > '7')
			break;

		value = (value << 3) | (uint64_t) (c - '0');
	}

	return value;
}

#endif // __LIBK__OCT2BIN_H__