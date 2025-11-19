#ifndef __LIBK__SYMBOLS_H__
#define __LIBK__SYMBOLS_H__

#include "libk/vector.h"
#include <libk/type.h>

typedef struct
{
    const char *name;
    uintptr_t   value;
    size_t      size;
} symbols_item;
define_vector(symbols_item);

typedef struct
{
    const char *name;
    vector(symbols_item) items;
} symbols;
typedef symbols *symbols_ptr;
define_vector(symbols_ptr);
typedef vector(symbols_ptr) symbols_ptr_vector_t;

void symbols_register(symbols *sym, const char *name, uintptr_t value, size_t size);

#endif // __LIBK__SYMBOLS_H__