#include "hal/acpi/acpi.h"
#include "hal/cpu/cpuid.h"
#include "libk/io.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "type.h"
#include <hal/acpi/hpet.h>
#include <str.h>

static volatile uintptr_t hpet_address = 0;
static uint64_t min_tick_ns = 0;
boolean_t hpet_available = 0;

boolean_t vxHPETIsAvailable() {
	return hpet_available;
}

static void hpet_write(uint32_t reg, uint64_t value) {
	mmio_outll((hpet_address + reg), value);
}

static uint64_t hpet_read(uint32_t reg) {
	return mmio_inll((hpet_address + reg));
}

void hpet_enable() {
	uint64_t cfg = hpet_read(HPET_GENERAL_CONFIG);
	cfg |= 1; // bit enable
	hpet_write(HPET_GENERAL_CONFIG, cfg);
}

void hpet_disable() {
	hpet_write(HPET_GENERAL_CONFIG, 0);
}

uint64_t vxHPETMinTickNs(void) {
	uint64_t min_tick = (hpet_read(HPET_GENERAL_CAP_ID) >> 32);
	uint64_t ns_per_tick = min_tick / 1000000;
	return ns_per_tick;
}

void hpet_level_timer_setup(uint32_t n, uint64_t tick_count, int irq) {
	uint32_t cap = hpet_read(HPET_TIMER_CONFIG(n)) >> 32;
	if ((cap & (1U << irq)) == 0) {
		LOG2_ERROR("HPET", "invalid irq %d, available %b", irq, cap);
		return;
	}

	hpet_write(HPET_TIMER_CONFIG(n),
		   (uint64_t) ((1 << 1) | (1 << 2) | (irq << 9) | (1 << 3)));
	hpet_write(HPET_TIMER_COMPARATOR(n), tick_count);
}

void vxHPETInitialize(uintptr_t addr) {
	struct hpet table;
	memcopy(&table, (void*) addr, sizeof(struct hpet));

	LOG2_INFO("HPET", "signature %s", table.header.signature);
	if (strncmp(table.header.signature, "HPET", 4) != 0)
		return;
	hpet_address = acpi_map_phys_page(
		(uint64_t) (table.address.address & ~((uint64_t) 0x1000 - 1)),
		2);
	LOG2_INFO("HPET", "address 0x%x", hpet_address);

	uint64_t min_tick = (hpet_read(HPET_GENERAL_CAP_ID) >> 32);
	min_tick_ns = min_tick / 1000000;
	LOG2_INFO("HPET", "minimum ticks : %lu fs (%d ns)", (uint64_t) min_tick,
		  min_tick_ns);

	uint64_t cap = hpet_read(0x0);
	uint8_t num_timers = ((cap >> 8) & 0x1F) + 1;
	LOG2_INFO("HPET", "num timers %d", num_timers);

	hpet_available = 1;

	hpet_disable();
	hpet_write(HPET_MAIN_COUNT, 0);
	hpet_enable();
}

uint64_t vxHPETGetMainCount() {
	return hpet_read(HPET_MAIN_COUNT);
}

void vxHPETSleep(uint64_t ns) {
	uint64_t ticks = ns / min_tick_ns;
	uint64_t start = vxHPETGetMainCount();
	while ((vxHPETGetMainCount() - start) < ticks)
		;
}
