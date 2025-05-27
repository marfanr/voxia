#ifndef __LIBK__HASH_H__
#define __LIBK__HASH_H__

static unsigned long
hash (const char *str, int max_size)
{
    unsigned long hash = 5381;
    int i = 0;
    while (*str)
        {
            hash = ((i++ + hash << 5) + hash) + *str++;
        }
    if (max_size == 0)
        return hash;
    return hash % max_size;
}

#endif // __LIBK__HASH_H__