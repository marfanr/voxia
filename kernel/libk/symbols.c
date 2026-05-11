#include "memory/kalloc.h"
#include <str.h>
#include <libk/symbols.h>

void symbols_register(symbols* sym, const char* name, uintptr_t value,
		      size_t size) {
	symbols_item i;
	i.name = kalloc(strlen(name));
	strcpy((char*) i.name, name);
	i.value = value;
	i.size = size;
	vector_push_back(&sym->items, i);
}
