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
#define TIMER_DIVIDE_CONFIG 0x3E0
#define TIMER_CURRENT_COUNT 0x390
#define APIC_TIMER_PERIOD (1 << 17)
#define APIC_TIMER_ONE_SHOT (0 << 17)
#define APIC_TIMER_DEADLINE (2 << 17)

#define APIC_LVT_TMR 0x0320
#define APIC_LVT_PERF 0x0340
#define APIC_LVT_LINT0 0x0350
#define APIC_LVT_LINT1 0x0360
#define APIC_LVT_ERR 0x0370
#define APIC_ESR 0x0280

void apicInitialize();
void apic_eoi();
void apic_write(uint32_t reg, uint32_t value);
uint32_t apic_read(uint32_t reg);
void apic_send_ipi(uint8_t vector, uint8_t dest);
void vxAPICCreateTimer(uint32_t type, double freq_us, uint8_t vector);
void vxAPICCreateDeadlineTimer(const uint8_t vector, const double freq_us);
bool vxTSChasInvariant(void);

#endif // __HAL__APIC__APIC_H__