#ifndef __LIBK__HASH_H__
#define __LIBK__HASH_H__

#include "libk/type.h"

static unsigned long
hash(const char *str, size_t max_size)
{
    unsigned long hash = 3141592653L;

    size_t i = 0;
    while (*str)
    {
        hash = (((i++ + hash) << i) + (hash << 8 * i)) + (*str++ >> 8 * i);
    }
    if (max_size == 0)
        return hash;
    return hash % max_size;
}

#endif // __LIBK__HASH_H__