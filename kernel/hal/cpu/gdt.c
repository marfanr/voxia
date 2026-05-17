#include "gdt.h"
#include "init/init.h"
#include "libk/serial.h"
#include "procc/thread.h"
#include <libk/debug/debug.h>
#include <str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

extern void reloadGDT(int cs, int ds);

static gdt_entry_t gdt_entries[14];
gdt_ptr_t gdt_ptr;
static lm_tss_t tss;

#define TSS_LOW 0x28ULL
#define TSS_HIGH 0x30ULL

uint8_t stack[4096] __attribute__((aligned(16)));

static gdt_each_core_t __gdt_entries[10];
uint8_t ap_stack_top[10][65536] __attribute__((aligned(16))); // 64KB

__attribute__((no_stack_protector)) void setup_gdt(int core) {
	gdt_entry_t* entries = __gdt_entries[core].entries;

	entries[0] = gdt_make_entry(0, 0, 0, 0);
	entries[1] = gdt_make_entry(0, 0xFFFF, 0x9A, 0x00);
	entries[2] = gdt_make_entry(0, 0xFFFF, 0x92, 0x00);
	entries[3] = gdt_make_entry(0, 0xFFFF, 0x9A, 0xCF);
	entries[4] = gdt_make_entry(0, 0xFFFF, 0x92, 0xCF);
	entries[5] = gdt_make_entry(0, 0, 0x9A, 0x20);
	entries[6] = gdt_make_entry(0, 0, 0x92, 0x00);
	entries[7] = gdt_make_entry(0, 0, 0xFA, 0x20);
	entries[8] = gdt_make_entry(0, 0, 0xF2, 0x00);

	lm_tss_t* _tss = &__gdt_entries[core].tss;
	_tss->rsp[0] =
		(uintptr_t) ap_stack_top[core] + sizeof(ap_stack_top[core]);
	_tss->rsp[1] = 0;
	_tss->rsp[2] = 0;
	_tss->reserved = 0;
	_tss->reserved2 = 0;
	_tss->reserved3 = 0;
	_tss->reserved4 = 0;
	_tss->ist[0] = _tss->ist[1] = _tss->ist[2] = _tss->ist[3] = 0;
	_tss->ist[4] = _tss->ist[5] = _tss->ist[6] = 0;
	_tss->iomap_base = sizeof(*_tss);

	uint64_t base = (uint64_t) _tss; // ← FIX: _tss bukan &_tss
	uint16_t limit = (uint16_t) (sizeof(*_tss) - 1);

	entries[9].limit_low = limit;
	entries[9].base_low = (uint16_t) (base & 0xFFFF);
	entries[9].base_middle = (uint8_t) ((base >> 16) & 0xFF);
	entries[9].access = 0x89;
	entries[9].flags = 0x00;
	entries[9].base_high = (uint8_t) ((base >> 24) & 0xFF);

	uint64_t* tss_high = (uint64_t*) ((uintptr_t) &entries[10]);
	*tss_high = (base >> 32) & 0xFFFFFFFF;

	gdt_ptr_t* _gdt_ptr = &__gdt_entries[core].pointer;
	_gdt_ptr->limit = (uint16_t) (sizeof(gdt_entry_t) * 11 - 1);
	_gdt_ptr->base = (uint64_t) entries;

	gdt_flush(*_gdt_ptr);
	__asm__ volatile("ltr %%ax" : : "a"((uint16_t) 0x48));
	reloadGDT(0x28, 0x30);
}

INIT(Gdt) {
	gdt_entries[0] = gdt_make_entry(0, 0, 0, 0); // 0x00 null segment
	//   16 bit
	gdt_entries[1] = gdt_make_entry(0, 0xFFFF, 0x9A,
					0x00); // 0x08 16 bit code segment
	gdt_entries[2] = gdt_make_entry(0, 0xFFFF, 0x92,
					0x00); // 0x10 16 bit data segment
	// 32 bit memiliki base dan limit yang sama dengan 16 bit
	// namun memiliki flag yang berbeda yaitu pada granularitas
	//   32 bit
	gdt_entries[3] = gdt_make_entry(0, 0xFFFF, 0x9A,
					0xCF); // 0x18 32 bit code segment
	gdt_entries[4] = gdt_make_entry(0, 0xFFFF, 0x92,
					0xCF); // 0x20 32 bit data segment
	// di 64 bit segmentasi diabaikan karena tidak digunakan
	// sehingga nilai base dan limit tidak diisi
	// 64 bit (ring 0)
	gdt_entries[5] =
		gdt_make_entry(0, 0, 0x9A, 0x20); // 0x28 64 bit code segment
	gdt_entries[6] =
		gdt_make_entry(0, 0, 0x92, 0x00); // 0x30 64 bit data segment
	// 64 bit userspace (ring 3)
	gdt_entries[7] =
		gdt_make_entry(0, 0, 0xFA, 0x20); // 0x38 64 bit code segment
	gdt_entries[8] =
		gdt_make_entry(0, 0, 0xF2, 0x00); // 0x40 64 bit data segment
	// 64 bit modulespace (ring 1)
	// gdt_entries[9]  = gdt_make_entry(0, 0, 0xBA, 0x20); // 0x48 64 bit code segment
	// gdt_entries[10] = gdt_make_entry(0, 0, 0xB2, 0x00); // 0x50 64 bit data segment

	// tss
	tss.rsp[0] = 0;
	tss.rsp[1] = 0;
	tss.reserved = 0;
	tss.reserved2 = 0;
	tss.reserved3 = 0;
	tss.reserved4 = 0;
	tss.ist[0] = 0;
	tss.ist[1] = 0;
	tss.ist[2] = 0;
	tss.ist[3] = 0;
	tss.ist[4] = 0;
	tss.ist[5] = 0;
	tss.ist[6] = 0;
	tss.iomap_base = 0;

	uint64_t base = (uint64_t) &tss;
	uint16_t limit = (uint16_t) (sizeof(tss) - 1);

	// TSS LOW descriptor (index 9 = selector 0x48)
	gdt_entries[9].limit_low = limit;
	gdt_entries[9].base_low = (uint16_t) (base & 0xFFFF);
	gdt_entries[9].base_middle = (uint8_t) ((base >> 16) & 0xFF);
	gdt_entries[9].access = 0x89; // Present | Type=TSS64 available
	gdt_entries[9].flags = 0x00;  // granularity=byte, limit sudah pas
	gdt_entries[9].base_high = (uint8_t) ((base >> 24) & 0xFF);

	// TSS HIGH descriptor (index 10) — hanya menyimpan base[63:32], sisanya 0
	// JANGAN pakai field gdt_entry_t karena mapping bitnya berbeda!
	uint64_t* tss_high = (uint64_t*) &gdt_entries[10];
	*tss_high = (base >> 32)
		    & 0xFFFFFFFF; // upper 32-bit base, reserved 32-bit = 0

	// Limit mencakup index 0..10 = 11 entry
	gdt_ptr.limit = (uint16_t) (sizeof(gdt_entry_t) * 11 - 1);
	gdt_ptr.base = (uint64_t) &gdt_entries;

	gdt_flush(gdt_ptr);
	__asm__ volatile("ltr %%ax" : : "a"((uint16_t) 0x48));
	reloadGDT(0x28, 0x30);
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