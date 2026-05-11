#include "hal/cpu/core.h"
#include "hal/cpu/msr.h"
#include "libk/io.h"
#include "libk/serial.h"
#include <hal/acpi/acpi.h>
#include <hal/apic/apic.h>
#include <hal/cpu/cpuid.h>

#define APIC_BASE_ADDR 0x1B
#define X2_APIC_ICR 0x830

#define ENABLE_APIC_BIT 1 << 11
#define ENABLE_X2_APIC_BIT 1 << 10

uintptr_t lapic_base_addr = 0;
uint8_t x2_apic_supported = 0;

void enable_x2apic() {
	uint64_t apic_base = vxRDMSR(0x1B);

	// step 1: AE=1, EXTD=0
	apic_base |= (1ULL << 11);
	apic_base &= ~(1ULL << 10);
	vxWRSR(0x1B, apic_base);

	// step 2: re-read
	apic_base = vxRDMSR(0x1B);

	// step 3: enable x2APIC
	apic_base |= (1ULL << 10);
	vxWRSR(0x1B, apic_base);

	x2_apic_supported = 1;
}

void apic_write(uint32_t reg, uint32_t value) {
	if (x2_apic_supported) {
		uint32_t msr = 0x800 + (reg >> 4);
		vxWRSR(msr, value);
	} else {
		mmio_outl(lapic_base_addr + reg, (uint32_t) value);
	}
}

uint32_t apic_read(uint32_t reg) {
	if (x2_apic_supported) {
		uint32_t msr = 0x800 + (reg >> 4);
		return vxRDMSR(msr);
	} else {
		return mmio_inl(lapic_base_addr + reg);
	}
}

void apicSetBaseAddr(uintptr_t addr) {
	lapic_base_addr = addr;
}

void apicInitialize() {
	uint32_t lo, hi;
	asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
	lo |= (1 << 11);
	asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0x1B));

	uint32_t eax, ebx, ecx, edx;
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	if (ecx & (1 << 21))
		enable_x2apic();

	uint32_t lapic_id;
	if (x2_apic_supported)
		lapic_id = (uint32_t) vxRDMSR(0x802); // x2APIC: ID full 32-bit
	else
		lapic_id = apic_read(0x20) >> 24;

	apic_write(APIC_LVT_TMR, 0x10000);
	apic_write(APIC_LVT_PERF, 0x10000);
	apic_write(APIC_LVT_LINT0, 0x10000);
	apic_write(APIC_LVT_LINT1, 0x10000);
	apic_write(APIC_LVT_ERR, 0x10000);
	apic_write(APIC_SVR, 0x1FF);
	apic_write(APIC_ESR, 0x00);
	apic_write(APIC_ESR, 0x00);
	apic_write(APIC_TPR, 0x00);
	apic_write(APIC_EOI, 0);

	if (!x2_apic_supported)
		apic_write(APIC_DFR, 0xFFFFFFFF);

	LOG2_DEBUG("lapic", "lapic id %d", lapic_id);
}

void apic_send_ipi(uint8_t vector, uint8_t dest) {
	apic_write(APIC_ICR_HIGH, (dest << 24));
	apic_write(APIC_ICR_LOW, (0b110 << 8) | vector);
}

void apic_eoi() {
	apic_write(APIC_EOI, 0);
}