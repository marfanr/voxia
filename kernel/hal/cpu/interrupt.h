#ifndef __HAL__CPU__INTERRUPT_H__
#define __HAL__CPU__INTERRUPT_H__
#include <type.h>

#define PIC1 0x20 /* IO base address for master PIC */
#define PIC2 0xA0 /* IO base address for slave PIC */
#define PIC1_COMMAND PIC1
#define PIC1_DATA (PIC1 + 1)
#define PIC2_COMMAND PIC2
#define PIC2_DATA (PIC2 + 1)

#define ICW1_ICW4 0x01	    /* Indicates that ICW4 will be present */
#define ICW1_SINGLE 0x02    /* Single (cascade) mode */
#define ICW1_INTERVAL4 0x04 /* Call address interval 4 (8) */
#define ICW1_LEVEL 0x08	    /* Level triggered (edge) mode */
#define ICW1_INIT 0x10	    /* Initialization - required! */

#define ICW4_8086 0x01	     /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO 0x02	     /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE 0x08  /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C /* Buffered mode/master */
#define ICW4_SFNM 0x10	     /* Special fully nested (not) */

#define MAX_INTERRUPTS 256
#define MAX_HANDLERS_PER_INTERRUPT 16

#define INTERRUPT_ATTR_USER 0x8E
#define INTERRUPT_ATTR_KERNEL 0xEE

typedef struct {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t type_attr;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t zero;
} __attribute__((packed)) interrupt_entry_t;

typedef struct {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed)) interrupt_pointers_t;

typedef struct {
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

	uint64_t int_no;
	uint64_t err_code;

	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
} interrupt_stack_frame_t;

typedef struct {
	uint8_t data[512];
} __attribute__((aligned(16))) fpu_state_t;

typedef struct {
	uint8_t mask;
	boolean_t use_default_isr;
	boolean_t configured;
	boolean_t allocated;
	void* handler[MAX_HANDLERS_PER_INTERRUPT];
} irq_entry_t;

typedef struct {
	uint8_t bitmap[MAX_INTERRUPTS];
	irq_entry_t irq_entries[MAX_INTERRUPTS];
	interrupt_entry_t interrupt_entries[MAX_INTERRUPTS];
	interrupt_pointers_t interrupt_pointers;
} interrupt_per_core_data_t;

void irq_register(uint8_t core, int n, void* handler, boolean_t use_default_isr,
		  uint16_t selector, uint8_t ist, uint8_t type_attr);
void irq_setup(uint16_t core);
uint16_t irq_alloc_entry(uint8_t core);

extern __attribute__((no_stack_protector)) void
vxInterruptHandler(interrupt_stack_frame_t* rsp, fpu_state_t* fpu);


#endif // __HAL__CPU__INTERRUPT_H__