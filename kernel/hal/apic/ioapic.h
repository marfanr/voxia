#ifndef __HAL__APIC__IOAPIC_H__
#define __HAL__APIC__IOAPIC_H__

#include <libk/type.h>

struct irq_gsi_map
{
    uint32_t gsi;
    uint16_t flags;
} __attribute__((aligned(8)));

void ioapic_setup(uintptr_t ioapic_base_addr);
void ioapic_add_irq_gsi_map(uint8_t irq, uint32_t gsi, uint16_t flags);
void vxIOAPICMapISR(uint8_t irq, uint8_t vector, uint8_t apic_id);

#endif // __HAL__APIC__IOAPIC_H__