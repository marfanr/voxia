#ifndef __HAL__CPU__INTERRUPT_H__
#define __HAL__CPU__INTERRUPT_H__
#include <libk/type.h>

#define PIC1 0x20 /* IO base address for master PIC */
#define PIC2 0xA0 /* IO base address for slave PIC */
#define PIC1_COMMAND PIC1
#define PIC1_DATA (PIC1 + 1)
#define PIC2_COMMAND PIC2
#define PIC2_DATA (PIC2 + 1)

#define ICW1_ICW4 0x01      /* Indicates that ICW4 will be present */
#define ICW1_SINGLE 0x02    /* Single (cascade) mode */
#define ICW1_INTERVAL4 0x04 /* Call address interval 4 (8) */
#define ICW1_LEVEL 0x08     /* Level triggered (edge) mode */
#define ICW1_INIT 0x10      /* Initialization - required! */

#define ICW4_8086 0x01       /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO 0x02       /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE 0x08  /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C /* Buffered mode/master */
#define ICW4_SFNM 0x10       /* Special fully nested (not) */

#define MAX_INTERRUPTS 256

#define INTERRUPT_ATTR_USER 0x8E
#define INTERRUPT_ATTR_KERNEL 0xEE

typedef struct
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) interrupt_entry_t;

typedef struct
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) interrupt_pointers_t;

typedef struct
{
    uint64_t rax, rbx, rcx, rdx, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, int_no,
        err_code, rip, cs, rflags, rsp, ss;
} __attribute__((packed)) interrupt_stack_frame_t;

// register interrupt ISR
void interrupt_register(uint8_t n, void *handler, uint16_t selector, uint8_t ist,
                        uint8_t type_attr);
// register interrupt handler with default ISR
void interrupt_register_handler(uint8_t n, void *handler);

typedef struct
{
    boolean_t use_default_isr;
    boolean_t configured;
    void     *handler;
} irq_entry_t;

void irq_register(uint8_t n, void *handler, boolean_t use_default_isr, uint16_t selector,
                  uint8_t ist, uint8_t type_attr);

#endif // __HAL__CPU__INTERRUPT_H__