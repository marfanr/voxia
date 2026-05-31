#ifndef __HAL_CPU_GDT_H__
#define __HAL_CPU_GDT_H__

#include <type.h>

typedef struct {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_middle;
	uint8_t access;
	uint8_t flags;
	uint8_t base_high;
} __attribute__((packed))
gdt_entry_t; // __attribute__((packed)) is used to tell the compiler not
	     // to optimize the struct

typedef struct {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct lm_tss {
	uint32_t reserved;
	uint64_t rsp[3];
	uint64_t reserved2;
	uint64_t ist[7];
	uint64_t reserved3 : 32;
	uint16_t reserved4;
	uint16_t iomap_base;
} __attribute__((packed)) lm_tss_t;

typedef struct {
	gdt_entry_t entries[14];
	gdt_ptr_t pointer;
	lm_tss_t tss;
} gdt_each_core_t;

gdt_entry_t
gdt_make_entry(uint32_t base, uint16_t limit, uint8_t access, uint8_t flags);
void gdt_flush(gdt_ptr_t gdt_ptr);

__attribute__((no_stack_protector)) void setup_gdt(int core);
void set_tss_stack(uint16_t core, uintptr_t stack_top);

#endif // __HAL_CPU_GDT_H__