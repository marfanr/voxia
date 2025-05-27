#include <libk/str/memcopy.h>

void memcopy(void *dest, void *src, size_t size) {
  uint8_t *d = (uint8_t *)dest;
  uint8_t *s = (uint8_t *)src;
  for (size_t i = 0; i < size; i++) {
    d[i] = s[i];
  }
}
