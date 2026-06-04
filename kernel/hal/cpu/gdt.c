#include "gdt.h"
#include "init/init.h"
#include "libk/serial.h"
#include "procc/thread.h"
#include <libk/debug/debug.h>
#include <str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

extern void reloadGDT(int cs, int ds);

#define TSS_LOW 0x28ULL
#define TSS_HIGH 0x30ULL

static gdt_each_core_t __gdt_entries[VOXIA_MAX_CORE];

__attribute__((no_stack_protector)) void setup_gdt(int core) {
	gdt_entry_t* entries = __gdt_entries[core].entries;

	entries[0] = gdt_make_entry(0, 0, 0, 0); // 0x0
	entries[1] = gdt_make_entry(0, 0xFFFF, 0x9A, 0x00); // 0x08
	entries[2] = gdt_make_entry(0, 0xFFFF, 0x92, 0x00);// 0x10
	entries[3] = gdt_make_entry(0, 0xFFFF, 0x9A, 0xCF);
	entries[4] = gdt_make_entry(0, 0xFFFF, 0x92, 0xCF);
	entries[5] = gdt_make_entry(0, 0, 0x9A, 0x20); // Kernel CS 0x28
	entries[6] = gdt_make_entry(0, 0, 0x92, 0x00); // Kernel DS 0x30
	entries[7] = gdt_make_entry(0, 0, 0xFA, 0xCF); // User CS 32-bit (Base for SYSRET) 0x38
	entries[8] = gdt_make_entry(0, 0, 0xF2, 0x00); // User SS 64-bit (Base + 8) 0x40
	entries[9] = gdt_make_entry(0, 0, 0xFA, 0x20); // User CS 64-bit (Base + 16) 0x48

	lm_tss_t* _tss = &__gdt_entries[core].tss;
	_tss->rsp[0] = 0;
	_tss->rsp[1] = 0;
	_tss->rsp[2] = 0;
	_tss->reserved = 0;
	_tss->reserved2 = 0;
	_tss->reserved3 = 0;
	_tss->reserved4 = 0;
	_tss->ist[0] = _tss->ist[1] = _tss->ist[2] = _tss->ist[3] = 0;
	_tss->ist[4] = _tss->ist[5] = _tss->ist[6] = 0;
	_tss->iomap_base = sizeof(*_tss);

	uint64_t base = (uint64_t) _tss;
	uint16_t limit = (uint16_t) (sizeof(*_tss) - 1);

	entries[10].limit_low = limit;
	entries[10].base_low = (uint16_t) (base & 0xFFFF);
	entries[10].base_middle = (uint8_t) ((base >> 16) & 0xFF);
	entries[10].access = 0x89;
	entries[10].flags = 0x00;
	entries[10].base_high = (uint8_t) ((base >> 24) & 0xFF);

	uint64_t* tss_high = (uint64_t*) ((uintptr_t) &entries[11]);
	*tss_high = (base >> 32) & 0xFFFFFFFF;

	gdt_ptr_t* _gdt_ptr = &__gdt_entries[core].pointer;
	_gdt_ptr->limit = (uint16_t) (sizeof(gdt_entry_t) * 14 - 1);
	_gdt_ptr->base = (uint64_t) entries;

	gdt_flush(*_gdt_ptr);
	__asm__ volatile("ltr %%ax" : : "a"((uint16_t) 0x50));
	reloadGDT(0x28, 0x30);
}

void set_tss_stack(uint16_t core, uintptr_t stack_top) {
	lm_tss_t* _tss = &__gdt_entries[core].tss;
	_tss->rsp[0] = stack_top;
}

INIT(Gdt) {
	setup_gdt(0);
}

gdt_entry_t
gdt_make_entry(uint32_t base, uint16_t limit, uint8_t access, uint8_t flags) {
	gdt_entry_t entry;
	entry.base_low = base & 0xFFFF;
	entry.base_middle = (base >> 16) & 0xFF;
	entry.base_high = (base >> 24) & 0xFF;
	entry.limit_low = limit;
	entry.flags = flags;
	entry.access = access;

	return entry;
}

void gdt_flush(gdt_ptr_t p) {
	__asm__ volatile("lgdt %0" : : "memory"(p));
}