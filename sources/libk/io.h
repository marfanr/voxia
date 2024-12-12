#ifndef __LIBK_IO_H__
#define __LIBK_IO_H__

#include <libk/type.h>

inline void outb(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// receives data from a IO port
inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));

  return ret;
}

inline void outl(uint16_t port, uint32_t value) {
  asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

inline uint32_t inl(uint16_t port) {
  uint32_t ret;
  asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));

  return ret;
}
#endif