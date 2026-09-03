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
	serial2_printf("GDT: Setting up core %d\n", core);
	gdt_entry_t* entries = __gdt_entries[core].entries;

	// Reset all entries
	memset(entries, 0, sizeof(gdt_entry_t) * 14);

	entries[0] = gdt_make_entry(0, 0, 0, 0); // 0x0
	entries[1] = gdt_make_entry(0, 0xFFFF, 0x9A, 0x00); // 0x08
	entries[2] = gdt_make_entry(0, 0xFFFF, 0x92, 0x00);// 0x10
	entries[3] = gdt_make_entry(0, 0xFFFF, 0x9A, 0xCF);
	entries[4] = gdt_make_entry(0, 0xFFFF, 0x92, 0xCF);
	entries[5] = gdt_make_entry(0, 0, 0x9A, 0x20); // Kernel CS 0x28
	entries[6] = gdt_make_entry(0, 0, 0x92, 0x00); // Kernel DS 0x30
	entries[7] = gdt_make_entry(0, 0, 0xFA, 0xCF); // User CS 32-bit 0x38
	entries[8] = gdt_make_entry(0, 0, 0xF2, 0x00); // User SS 64-bit 0x40
	entries[9] = gdt_make_entry(0, 0, 0xFA, 0x20); // User CS 64-bit 0x48

	lm_tss_t* _tss = &__gdt_entries[core].tss;
	memset(_tss, 0, sizeof(lm_tss_t));
	_tss->iomap_base = sizeof(*_tss);

	uint64_t base = (uintptr_t) _tss;
	uint16_t limit = (uint16_t) (sizeof(*_tss) - 1);

	// TSS Descriptor (16-byte)
	entries[10].limit_low = limit;
	entries[10].base_low = (uint16_t) (base & 0xFFFF);
	entries[10].base_middle = (uint8_t) ((base >> 16) & 0xFF);
	entries[10].access = 0x89; // Present, Executable, Accessible TSS
	entries[10].flags = (uint8_t) ((limit >> 16) & 0x0F);
	entries[10].base_high = (uint8_t) ((base >> 24) & 0xFF);

	uint64_t* tss_high = (uint64_t*) ((uintptr_t) &entries[11]);
	*tss_high = (base >> 32); // Upper 32 bits of base

	gdt_ptr_t* _gdt_ptr = &__gdt_entries[core].pointer;
	_gdt_ptr->limit = (uint16_t) (sizeof(gdt_entry_t) * 14 - 1);
	_gdt_ptr->base = (uintptr_t) entries;

	serial2_printf("GDT: Flushing GDT ptr at %p (base=%p)\n", _gdt_ptr, (void*)_gdt_ptr->base);
	gdt_flush(*_gdt_ptr);

	serial2_printf("GDT: Loading TR\n");
	__asm__ volatile("ltr %%ax" : : "a"((uint16_t) 0x50));

	serial2_printf("GDT: Reloading segments\n");
	reloadGDT(0x28, 0x30);
	serial2_printf("GDT: Done\n");
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
	__asm__ volatile("lgdt %0" : : "m"(p));
}
