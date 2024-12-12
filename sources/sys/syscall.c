#include "syscall.h"
#include <dev/graphic/fb.h>
#include <libk/debug/debug.h>
#include <libk/serial.h>

void sys_read(int descriptor, void *buffer, uint64_t length) {}

void sys_write(int descriptor, const char *buffer, uint64_t length) {

  uint64_t rdi = 0;
  asm volatile("mov %%r8, %0" : "=r"(rdi));
  serial_trace("r8 : 0x%x\n", rdi);

  if (descriptor == 0) {
    serial_printf(buffer);
  } else if (descriptor == 1) {
    serial_trace("length : %d\n", length);
    KDEBUG(DEBUG_LEVEL_INFO, buffer);
  }
}

void sys_open(const char *path, uint16_t opt) {}

void sys_load(const char *path) {}

void sys_alloc(uint64_t size) {}