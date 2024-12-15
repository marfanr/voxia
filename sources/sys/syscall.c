#include "syscall.h"
#include "descriptor.h"
#include <dev/cpu/apic/apic.h>
#include <dev/cpu/int/idt.h>
#include <dev/graphic/fb.h>
#include <libk/console/console.h>
#include <libk/debug/debug.h>
#include <libk/serial.h>
#include <memory/phys_base_allocator.h>

void syscall(void *stack_adr) {
  unsigned long *stack = (unsigned long *)stack_adr;
  uint64_t rax = stack[0];
  uint64_t rdi = stack[1];
  uint64_t rsi = stack[2];
  uint64_t rdx = stack[3];
  uint64_t rcx = stack[4];
  uint64_t r8 = stack[5];
  uint64_t r9 = stack[6];

  serial_trace("rax : %d rdi : %d rsi : %s\n", rax, rdi, (const char *)rsi);
  // serial_trace("rsi : %s\n\n", (const char *)rsi);

  uint64_t ret = 0;
  switch (rax) {
  case SYSCALL_WRITE:
    ret = sys_write(rdi, (const char *)rsi, rdx);
    break;

  case SYSCALL_READ:
    // sys_read(rdi, (char *)rsi, rdx);
    break;

  case SYSCALL_ALLOC:
    ret = (unsigned long)sys_alloc(rdi);
    break;

  default:
    break;
  }
  apic_eoi();

  asm volatile("movq %0, %%rax" : : "r"(ret));
}

void sys_read(uint64_t descriptor, char *buffer, uint64_t length) { return 0; }

/**
 * @brief write to console or serial port
 *
 * @param descriptor
 * @param buffer
 * @param length
 */
uint64_t sys_write(uint64_t descriptor, const char *buffer, uint64_t length) {
  if (descriptor == 0) {
    for (uint64_t i = 0; i < length; i++) {
      serial_putc(buffer[i]);
    }
  } else if (descriptor == 1) {
    console_print(buffer, length);
  }
  return length;
}

void sys_open(const char *path, uint16_t opt) {}

void sys_load(const char *path) {}

uintptr_t sys_alloc(uint64_t size) {
  uintptr_t buf = (uintptr_t)phys_base_alloc(size);
  serial_trace("buf : 0x%x\n", buf);
  return buf;
}