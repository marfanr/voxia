#include <hal/cpu/core.h>
#include "hal/cpu/cpuid.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "hal/timer/timer.h"
#include "init/init.h"
#include "libk/io.h"
#include "libk/serial.h"
#include <hal/acpi/hpet.h>
#include <hal/apic/apic.h>

void vxInitializeAPICTimer();

static uint64_t calibrated_ticks_1ns = 0;
static uint64_t calibrated_tsc_ticks_1us = 0;

extern uint8_t x2_apic_supported;

static uint64_t vxAPICReadTSC() {
	uint32_t eax, edx;
	__asm__ volatile("rdtsc" : "=a"(eax), "=d"(edx));
	return ((uint64_t) edx << 32) | eax;
}

static boolean_t vxAPICIsTSCDeadlineSupported() {
	uint32_t eax, ebx, ecx, edx;
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	return (ecx & (1 << 24)) != 0;
}

bool vxTSChasInvariant(void) {
	uint32_t edx, unused;
	cpuid(0x80000007, 0, &unused, &unused, &unused, &edx);
	return edx & (1 << 8);
}

static void vxAPICTimerCalibrationUsingHPET() {
	for (uint64_t i = 0; i < 5; i++) {
		// Calibrate APIC Bus Timer
		apic_write(TIMER_DIVIDE_CONFIG, 0b1011);

		LOG2_DEBUG("APIC_TIMER", "[APIC] calibrating core %d....",
			   get_current_core_data()->core_id);
		hpet_disable();
		hpet_enable();

		time_counter_t counter = {0};
		init_timer_counter(&counter);

		apic_write(LVT_TIMER, 0x20 | APIC_TIMER_ONE_SHOT);
		apic_write(TIMER_INITIAL_COUNT, 0xFFFFFFFF);

		uint64_t tsc_start = vxAPICReadTSC();

		while (get_timer_counter_count(&counter) < 1000)
			;

		uint64_t tsc_end = vxAPICReadTSC();
		uint64_t elapsed_time_us =
			get_timer_counter_count_ns(&counter) / 1000;

		uint64_t elapsed_apic =
			0xFFFFFFFF - apic_read(TIMER_CURRENT_COUNT);

		uint64_t apic_ticks_per_us = 0;
		if (elapsed_time_us > 0)
			apic_ticks_per_us = elapsed_apic / elapsed_time_us;

		uint64_t tsc_ticks_per_us = 0;
		if (elapsed_time_us > 0)
			tsc_ticks_per_us = (tsc_end - tsc_start) / elapsed_time_us;

		// Running average
		calibrated_ticks_1ns =
			(calibrated_ticks_1ns * i + apic_ticks_per_us) / (i + 1);
		calibrated_tsc_ticks_1us =
			(calibrated_tsc_ticks_1us * i + tsc_ticks_per_us) / (i + 1);
	}

	LOG2_DEBUG("APIC_TIMER", "[APIC] calibrated apic timer done. TSC ticks/us: %ld", calibrated_tsc_ticks_1us);
}

void vxInitializeAPICTimer() {
	if (get_current_core_cpuid() == 0) {
		vxAPICTimerCalibrationUsingHPET();
	} else {
		while (__atomic_load_n(&calibrated_ticks_1ns, __ATOMIC_ACQUIRE)
		       == 0)
			__asm__ volatile("pause");
	}
}

#define APIC_TIMER_MASKED (1 << 16)

#define APIC_TIMER_MIN_VECTOR 0x20

void vxAPICCreateTimer(uint32_t type, uint64_t interval_us, uint16_t vector) {
	if (vector < APIC_TIMER_MIN_VECTOR || vector > 0xFE) {
		LOG_ERROR("APIC_TIMER", "Invalid vector: 0x%x", vector);
		return;
	}

	if (interval_us == 0) {
		LOG_ERROR("APIC_TIMER", "Interval must be > 0");
		return;
	}

	if (calibrated_ticks_1ns == 0) {
		LOG_ERROR("APIC_TIMER", "APIC timer not calibrated");
		return;
	}

	if (type == APIC_TIMER_PERIOD || !vxAPICIsTSCDeadlineSupported()) {
		if (type != APIC_TIMER_ONE_SHOT && type != APIC_TIMER_PERIOD) {
			LOG_ERROR("APIC_TIMER", "Invalid timer type: 0x%x", type);
			return;
		}

		uint64_t count = calibrated_ticks_1ns * interval_us;

		if (count > 0xFFFFFFFF) {
			LOG_WARN("APIC_TIMER", "Interval too long, truncated");
			count = 0xFFFFFFFF;
		}

		apic_write(TIMER_DIVIDE_CONFIG, 0x0B);

		uint32_t lvt = (vector & 0xFF) | type | APIC_TIMER_MASKED;
		apic_write(LVT_TIMER, lvt);

		lvt = apic_read(LVT_TIMER);
		apic_write(LVT_TIMER, lvt & (uint32_t)~APIC_TIMER_MASKED);
		apic_write(TIMER_INITIAL_COUNT, (uint32_t) count);
	} else {
		// Use TSC-Deadline for One-Shot
		const uint64_t tsc_delta = calibrated_tsc_ticks_1us * interval_us;
		const uint64_t deadline = vxAPICReadTSC() + tsc_delta;
		
		apic_write(LVT_TIMER, (vector & 0xFF) | APIC_TIMER_DEADLINE);
		vxWRSR(0x6E0, deadline);
	}
}

void vxAPICCreateDeadlineTimer(const uint8_t vector, const uint64_t freq_us) {
	apic_write(LVT_TIMER, (vector & 0xFF) | APIC_TIMER_DEADLINE);

	const uint64_t tsc_delta = calibrated_tsc_ticks_1us * freq_us;
	const uint64_t deadline = vxAPICReadTSC() + tsc_delta;
	vxWRSR(0x6E0, deadline);
}