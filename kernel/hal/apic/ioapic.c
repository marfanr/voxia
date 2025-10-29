#include "libk/io.h"
#include <hal/apic/ioapic.h>

static struct irq_gsi_map irq_gsi_table[32] = {0};

#define IOAPICVER 0x01
#define IOAPICARB 0x02
#define IOAPICREDTBL(n) (0x10 + 2 * n) // lower-32bits (add +1 for upper 32-bits)

void
write_ioapic_register(const uintptr_t apic_base, const uint8_t offset, const uint32_t val)
{
    mmio_outl(apic_base, offset);
    mmio_outl(apic_base + 0x10, val);
}

uint32_t
read_ioapic_register(const uintptr_t apic_base, const uint8_t offset)
{
    mmio_outl(apic_base, offset);
    return mmio_inl(apic_base + 0x10);
}

void
ioapic_setup(uintptr_t ioapic_base_addr)
{
    // tandai smeua ioapic termasking
    for (int i = 0; i < 24; i++)
    {
        write_ioapic_register((uintptr_t)ioapic_base_addr, IOAPICREDTBL(i), 1 << 16);
        write_ioapic_register((uintptr_t)ioapic_base_addr, IOAPICREDTBL(i) + 1, 0);
    }

    // nyalakan irq 2 untuk HPET
    write_ioapic_register((uintptr_t)ioapic_base_addr, IOAPICREDTBL(2), 0 | 0x30);
    write_ioapic_register((uintptr_t)ioapic_base_addr, IOAPICREDTBL(2) + 1, 0 << 24);

    write_ioapic_register((uintptr_t)ioapic_base_addr, IOAPICREDTBL(10), 0 | 0x31);
    write_ioapic_register((uintptr_t)ioapic_base_addr, IOAPICREDTBL(10) + 1, 0 << 24);
}

void
ioapic_add_irq_gsi_map(uint8_t irq, uint32_t gsi, uint16_t flags)
{
    irq_gsi_table[irq].gsi   = gsi;
    irq_gsi_table[irq].flags = flags;
}