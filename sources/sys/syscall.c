#include "syscall.h"
#include "descriptor.h"
#include <dev/cpu/int/idt.h>
#include <dev/graphic/fb.h>
#include <libk/console/console.h>
#include <libk/debug/debug.h>
#include <libk/serial.h>

void syscall(uint64_t rsp) {
  uint64_t rdi = ((uint64_t *)rsp)[0];
  uint64_t rsi = ((uint64_t *)rsp)[1];
  uint64_t rdx = ((uint64_t *)rsp)[2];
  uint64_t rax = ((uint64_t *)rsp)[3];
  // serial_trace("rdi : %d\n", rdi);

  switch (rax) {
  case SYSCALL_WRITE:
    sys_write(rdi, (const char *)rsi, rdx);
    break;

  default:
    break;
  }
}
void sys_write(uint64_t descriptor, const char *buffer, uint64_t length) {
  if (descriptor == 0) {
    for (uint64_t i = 0; i < length; i++) {
      serial_putc(buffer[i]);
    }
  } else if (descriptor == 1) {
    console_print(buffer, length);
  }
}

void sys_open(const char *path, uint16_t opt) {}

void sys_load(const char *path) {}

void sys_alloc(uint64_t size) {}