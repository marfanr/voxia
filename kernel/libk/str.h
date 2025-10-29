#ifndef __LIBK__STR_H__
#define __LIBK__STR_H__

#include <libk/string.h>
#include <libk/type.h>
#include <libk/vector.h>

void memcopy(void *dest, void *src, size_t size);
void memset(void *ptr, uint8_t value, size_t num);

/**
 * @brief Menghitung panjang string.
 *
 * Fungsi ini menghitung panjang string hingga karakter null pertama.
 *
 * @param s Pointer ke string.
 * @return Panjang string.
 */
size_t strlen(const char *s);

/**
 * @brief Membandingkan dua string secara leksikografis hingga n karakter.
 *
 * @param s1 Pointer ke string pertama.
 * @param s2 Pointer ke string kedua.
 * @param n Jumlah karakter yang akan dibandingkan.
 * @return Nilai negatif jika s1 < s2, nilai positif jika s1 > s2, dan 0 jika s1
 * = s2.
 */
int strncmp(const char *s1, const char *s2, size_t n);

void strcpy(char *dest, const char *src);

typedef const char *__str;
define_vector(string);
void  explode(const char *path, const char delim, vector(string) * out);
char *rtrim(char *str);
#endif // __LIBK__STR_H__