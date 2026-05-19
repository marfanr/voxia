#ifndef __LIBK__HASH_H__
#define __LIBK__HASH_H__

#include "libk/type.h"

uint64_t hash(const char* str, size_t max_size);
uint32_t hash32(const char* str, size_t max_size);

#endif // __LIBK__HASH_H__