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
	uint32_t lo, hi;
	asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
	lo |= (1 << 11);
	asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0x1B));

	uint32_t eax, ebx, ecx, edx;
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	if ((ecx & (1 << 21))) {
		// TODO: wile be handled soon
		LOG2_INFO("APIC", "x2APIC available");
	}

	apic_write(APIC_TPR, 0x00);
	apic_write(APIC_DFR, 0xFFFFFFFF);
	apic_write(APIC_SVR, 0xff | 0x100);

	uint32_t lapic_id = apic_read(0x20) >> 24;

	LOG2_DEBUG("lapic", "lapic id %d", lapic_id);
}

void apic_send_ipi(uint8_t vector, uint8_t dest) {
	apic_write(APIC_ICR_HIGH, (dest << 24));
	apic_write(APIC_ICR_LOW, (0b110 << 8) | vector);
}

void apic_eoi() { apic_write(APIC_EOI, 0); }