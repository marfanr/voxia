#ifndef __LIBK__STR__STRLEN_H__
#define __LIBK__STR__STRLEN_H__

#include <libk/type.h>

/**
 * @brief Menghitung panjang string.
 *
 * Fungsi ini menghitung panjang string hingga karakter null pertama.
 *
 * @param s Pointer ke string.
 * @return Panjang string.
 */
static inline size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len])
    {
        len++;
    }
    return len;
}

#endif // __LIBK__STR__STRLEN_H__