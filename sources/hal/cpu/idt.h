#ifndef __HAL__CPU__IDT_H__
#define __HAL__CPU__IDT_H__

#include <libk/type.h>

typedef struct {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t ist;
  uint8_t type_attr;
  uint16_t offset_mid;
  uint32_t offset_high;
  uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
  uint64_t rax, rbx, rcx, rdx, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14,
      r15, int_no, err_code, rip, cs, rflags, rsp, ss;
} registers_t;

void idt_setup(void);
idt_entry_t add_idt_entry(void *offset, uint16_t selector, uint8_t ist,
                          uint8_t type_attr);

#endif // __HAL__CPU__IDT_H__