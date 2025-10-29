#include "init/init.h"
#include "libk/serial.h"
#include <hal/acpi/hpet.h>
#include <hal/apic/apic.h>

uint64_t calibrated_ticks_1ms = 0;

#define APIC_TIMER_PERIOD (1 << 17)
#define APIC_TIMER_ONE_SHOT (0 << 17)

INIT(apic_timer)
{
    uint64_t tick_ns    = hpet_min_tick_ns();
    uint64_t hpet_ticks = ms2ns(1) / tick_ns; // target HPET ticks

    apic_write(LVT_TIMER, 0x20 | APIC_TIMER_ONE_SHOT); // vector=0x20, one-shot
    apic_write(TIMER_INITIAL_COUNT, 0xFFFFFFFF);

    uint64_t start = hpet_read(HPET_MAIN_COUNT);
    while ((hpet_read(HPET_MAIN_COUNT) - start) < hpet_ticks)
        ;

    uint32_t elapsed       = 0xFFFFFFFF - apic_read(TIMER_CURRENT_COUNT);
    float    apic_freq_mhz = elapsed / (1 / 1000.0f);
    LOG_DEBUG("APIC_TIMER", "[APIC] Timer elapsed ticks: %d, freq ~ %.2f MHz", elapsed,
              apic_freq_mhz);
    calibrated_ticks_1ms = elapsed;

    // 5️⃣ setup APIC timer periodic 1 ms
    apic_write(LVT_TIMER, (1 << 16));   // vector=0x20, periodic
    apic_write(TIMER_INITIAL_COUNT, 0); // gunakan tick hasil kalibrasi
    LOG_DEBUG("APIC_TIMER", "[APIC] Timer periodic 1ms setup done.");

    // matikan hpet
    hpet_disable();
}

void
timer_handle(void)
{
    apic_eoi();
}

void
timer_create_one_shoot(uint64_t ms, void (*handler)(void))
{
}

void
scheduler_timer_handler()
{
}