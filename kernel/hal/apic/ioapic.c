#include "libk/io.h"
#include <hal/apic/ioapic.h>

static struct irq_gsi_map irq_gsi_table[32] = {0};

#define IOAPICVER 0x01
#define IOAPICARB 0x02
#define IOAPICREDTBL(n)                                                        \
	(0x10 + 2 * n) // lower-32bits (add +1 for upper 32-bits)

static uintptr_t ioapic_base_addr = 0;

void write_ioapic_register(const uintptr_t apic_base, const uint8_t offset,
			   const uint32_t val) {
	mmio_outl(apic_base, offset);
	mmio_outl(apic_base + 0x10, val);
}

uint32_t read_ioapic_register(const uintptr_t apic_base, const uint8_t offset) {
	mmio_outl(apic_base, offset);
	return mmio_inl(apic_base + 0x10);
}

uint32_t ioapic_isr_get_vector(uint8_t irq) {
	auto reg = read_ioapic_register(ioapic_base_addr, IOAPICREDTBL(irq));
	return reg & 0xFF;
}

void vxIOAPICMapISR(uint8_t irq, uint8_t vector, uint8_t apic_id) {
	uint32_t low = 0;
	uint32_t high = 0;

	low |= vector;
	low |= (0 << 8); // delivery mode
	low |= (0 << 11);
	low |= (0 << 13); // polarity
	low |= (0 << 15); // trigger mode

	high |= (apic_id << 24);

	// set redirection table
	write_ioapic_register((uintptr_t) ioapic_base_addr, IOAPICREDTBL(irq),
			      low);
	write_ioapic_register((uintptr_t) ioapic_base_addr,
			      IOAPICREDTBL(irq) + 1, high);
}

void ioapic_setup(uintptr_t ioapic_addr) {
	ioapic_base_addr = ioapic_addr;
	// tandai smeua ioapic termasking
	for (int i = 0; i < 24; i++) {
		write_ioapic_register((uintptr_t) ioapic_addr, IOAPICREDTBL(i),
				      1 << 16);
		write_ioapic_register((uintptr_t) ioapic_addr,
				      IOAPICREDTBL(i) + 1, 0);
	}

	// vxIOAPICMapISR(11, 0x56, 0);
	// nyalakan irq 2 untuk HPET
	// write_ioapic_register((uintptr_t)ioapic_base_addr, IOAPICREDTBL(2), 0
	// | 0x30); write_ioapic_register((uintptr_t)ioapic_base_addr,
	// IOAPICREDTBL(2) + 1, 0 << 24);

	// virtio
	// write_ioapic_register((uintptr_t)ioapic_base_addr, IOAPICREDTBL(10),
	//                       0 | 55);
	// write_ioapic_register((uintptr_t)ioapic_base_addr, IOAPICREDTBL(10) +
	// 1,
	//                       1 << 24);
}

void ioapic_add_irq_gsi_map(uint8_t irq, uint32_t gsi, uint16_t flags) {
	irq_gsi_table[irq].gsi = gsi;
	irq_gsi_table[irq].flags = flags;
}