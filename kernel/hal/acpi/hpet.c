#include "hal/acpi/acpi.h"
#include "libk/io.h"
#include "libk/serial.h"
#include "libk/type.h"
#include <hal/acpi/hpet.h>
#include <libk/str.h>

static uintptr_t hpet_address   = 0;
static uint64_t  min_tick_ns    = 0;
boolean_t        hpet_available = 0;

void
hpet_write(uint32_t reg, uint64_t value)
{
    mmio_outll((hpet_address + reg), value);
}

uint64_t
hpet_read(uint32_t reg)
{
    return mmio_inll((hpet_address + reg));
}

void
hpet_enable()
{
    hpet_write(HPET_GENERAL_CONFIG, 1);
    LOG_DEBUG("HPET", "enable HPET");
}

void
hpet_disable()
{
    hpet_write(HPET_GENERAL_CONFIG, 0);
    LOG_DEBUG("HPET", "disable HPET");
}

uint64_t
hpet_min_tick_ns(void)
{
    uint64_t min_tick = (hpet_read(HPET_GENERAL_CAP_ID) >> 32);
    min_tick_ns       = min_tick / 1000000.0;
    return min_tick_ns;
}

void
hpet_level_timer_setup(int n, uint64_t tick_count, int irq)
{
    uint32_t cap = hpet_read(HPET_TIMER_CONFIG(n)) >> 32;
    if ((cap & (1U << irq)) == 0)
    {
        LOG_ERROR("HPET", "invalid irq %d, available %b", irq, cap);
        return;
    }

    hpet_write(HPET_TIMER_CONFIG(n), (1 << 1) | (1 << 2) | (irq << 9) | (1 << 3));
    hpet_write(HPET_TIMER_COMPARATOR(n), tick_count);
}

void
hpet_initialize(uintptr_t addr)
{
    struct hpet *hpet = (struct hpet *)addr;
    LOG_INFO("HPET", "signature %s", hpet->header.signature);
    if (strncmp(hpet->header.signature, "HPET", 4) != 0)
        return;
    hpet_address = acpi_map_phys_page(hpet->address.address, 2);
    LOG_INFO("HPET", "address 0x%x", hpet_address);

    uint64_t min_tick = (hpet_read(HPET_GENERAL_CAP_ID) >> 32);
    min_tick_ns       = min_tick / 1000000.0;
    LOG_INFO("HPET", "minimum ticks : %lu fs (%d ns)", (uint64_t)min_tick, min_tick_ns);

    uint64_t cap        = hpet_read(0x0);
    uint8_t  num_timers = ((cap >> 8) & 0x1F) + 1;
    LOG_INFO("HPET", "num timers %d", num_timers);

    hpet_available = 1;

    // reset main counter ke 0
    hpet_disable();
    hpet_write(HPET_MAIN_COUNT, 0);
    hpet_enable();

    // hpet_level_timer_setup(0, ms2ns(1) / min_tick_ns, 2);
}