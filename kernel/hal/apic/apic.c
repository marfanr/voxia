#include "hal/cpu/core.h"
#include "libk/io.h"
#include "libk/serial.h"
#include <hal/acpi/acpi.h>
#include <hal/apic/apic.h>
#include <hal/cpu/cpuid.h>

uintptr_t lapic_base_addr = 0;

void apic_write(uint32_t reg, uint32_t value) {
	mmio_outl((lapic_base_addr + reg), value);
}

uint32_t apic_read(uint32_t reg) { return mmio_inl((lapic_base_addr + reg)); }

void apicSetBaseAddr(uintptr_t addr) { lapic_base_addr = addr; }

void apicInitialize() {

	// nyalakan apic
	uint32_t lo, hi;
	asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
	lo |= (1 << 11);
	asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0x1B));

	uint32_t lapic_id = apic_read(0x20) >> 24;

	uint32_t eax, ebx, ecx, edx;
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	if ((ecx & (1 << 21))) {
		// TODO: wile be handled soon
		LOG2_INFO("APIC", "x2APIC available di id %d", lapic_id);
	}

	// 3. Masking semua LVT (Local Vector Table)
	// Mencegah interrupt liar (timer, hardware error, dll) mengganggu setup
	// core
	apic_write(APIC_LVT_TMR, 0x10000); // Mask Bit 16
	apic_write(APIC_LVT_PERF, 0x10000);
	apic_write(APIC_LVT_LINT0, 0x10000);
	apic_write(APIC_LVT_LINT1, 0x10000);
	apic_write(APIC_LVT_ERR, 0x10000);

	// 4. Software Enable APIC via Spurious Interrupt Vector Register (SVR)
	// Bit 8 (0x100) = Enable, 0xFF = Vector Spurious Interrupt
	apic_write(APIC_SVR, 0x1FF);

	apic_write(APIC_ESR, 0x00);
	apic_write(APIC_ESR, 0x00);

	// priority task register
	apic_write(APIC_TPR, 0x00);

	apic_write(APIC_EOI, 0);
	apic_write(APIC_DFR, 0xFFFFFFFF);

	LOG2_DEBUG("lapic", "lapic id %d", lapic_id);
}

void apic_send_ipi(uint8_t vector, uint8_t dest) {
	apic_write(APIC_ICR_HIGH, (dest << 24));
	apic_write(APIC_ICR_LOW, (0b110 << 8) | vector);
}

void apic_eoi() { apic_write(APIC_EOI, 0); }