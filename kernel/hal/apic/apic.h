#ifndef __HAL__APIC__APIC_H__
#define __HAL__APIC__APIC_H__

#include <libk/type.h>

#define APIC_TPR 0x80
#define APIC_DFR 0xE0
#define APIC_LDR 0xD0
#define APIC_SVR 0xF0
#define APIC_EOI 0xB0
#define APIC_ICR_LOW 0x300
#define APIC_ICR_HIGH 0x310
#define APIC_LOGIC_DEST 0xD0
#define APIC_ARBITATION_PRIOR 0x090
#define APIC_IRR 0x200
#define APIC_IEA 0x480
#define LVT_TIMER 0x320
#define TIMER_INITIAL_COUNT 0x380
#define TIMER_CURRENT_COUNT 0x390

void     apic_initialize(uintptr_t apic_base_addr);
void     apic_eoi();
void     apic_write(uint32_t reg, uint32_t value);
uint32_t apic_read(uint32_t reg);
void     apic_timer_initialize();
#endif // __HAL__APIC__APIC_H__