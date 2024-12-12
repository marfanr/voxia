#ifndef __LIBK__STR__STRNCMP_H__
#define __LIBK__STR__STRNCMP_H__

/**
 * @brief Membandingkan dua string secara leksikografis hingga n karakter.
 *
 * @param s1 Pointer ke string pertama.
 * @param s2 Pointer ke string kedua.
 * @param n Jumlah karakter yang akan dibandingkan.
 * @return Nilai negatif jika s1 < s2, nilai positif jika s1 > s2, dan 0 jika s1
 * = s2.
 */
static inline int strncmp(const char *s1, const char *s2, size_t n) {
  while (n-- != 0) {
    if (*s1 != *s2++)
      return *(unsigned char *)s1 - *(unsigned char *)--s2;
    if (*s1++ == 0)
      break;
  }
  return 0;
}

#endif // __LIBK__STR__STRNCMP_H__