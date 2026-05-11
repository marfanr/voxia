#ifndef __MODULES__KERNEL_HEADER_H__
#define __MODULES__KERNEL_HEADER_H__

#include "libk/symbols.h"
#include <vector.h>
#include <type.h>

typedef struct {
	const char* name;
	uintptr_t value;
	size_t size;
} kernel_symbol;

kernel_symbol* kernel_resolve_symbol(const char* name);
symbols_ptr kernel_get_symbols();

#endif // __MODULES__KERNEL_HEADER_H__