#ifndef __LIBK__HASH_H__
#define __LIBK__HASH_H__

#include "libk/type.h"

static uint64_t
hash(const char *str, size_t max_size)
{
    uint64_t hash = 0xcbf29ce484222325ULL;

    size_t i = 0;
    while (*str)
    {
        hash *= 0xa0761d6478bd642fULL;
        hash ^= hash >> 32;
        hash *= 0x9e3779b97f4a7c15ULL;
        hash ^= hash >> 32;
        hash *= 0x9e3779b97f4a7c15ULL;
        hash ^= hash >> 32;
        hash ^= *str++;
    }
    if (max_size == 0)
        return hash;
    return hash % max_size;
}

#endif // __LIBK__HASH_H__