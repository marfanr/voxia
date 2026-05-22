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
static uint64_t calibrated_tsmc_freq_1ms = 0;

extern uint8_t x2_apic_supported;

static uint64_t vxAPICReadTSC() {
	uint32_t eax, edx;
	__asm__ volatile("rdtsc" : "=a"(eax), "=d"(edx));
	return ((uint64_t) edx << 32) | eax;
}

// static void calibrate_tsmc_backend(interrupt_stack_frame_t* _) {
// 	tsmc_calibrated = 1;
// }

// static boolean_t vxAPICIsTSMCSupported() {
// 	uint32_t eax, ebx, ecx, edx;
// 	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
// 	return (ecx & (1 << 24)) != 0;
// }

bool vxTSChasInvariant(void) {
	uint32_t edx, unused;
	cpuid(0x80000007, 0, &unused, &unused, &unused, &edx);
	return edx & (1 << 8);
}

static void vxAPICTimerCalibrationUsingHPET() {

	for (uint64_t i = 0; i < 5; i++) {
		apic_write(TIMER_DIVIDE_CONFIG, 0b1011);

		LOG2_DEBUG("APIC_TIMER", "[APIC] calibrating core %d....",
			   get_current_core_data()->core_id);
		hpet_disable();
		// TODO: buat utility untuk handle ini
		// hpet_write(HPET_MAIN_COUNT, 0);
		hpet_enable();

		time_counter_t counter = {0};
		init_timer_counter(&counter);
		// LOG2_INFO("APIC TIMER", "ok");

		apic_write(LVT_TIMER, 0x20 | APIC_TIMER_ONE_SHOT);
		apic_write(TIMER_INITIAL_COUNT, 0xFFFFFFFF);

		// Tunggu 1000 us (1ms) — pakai integer, bukan 1e3
		while (get_timer_counter_count(&counter) < 1000)
			;

		// us → ns: bagi 1000, bukan kali 1e-3
		uint64_t elapsed_time_us =
			get_timer_counter_count_ns(&counter) / 1000;

		uint64_t elapsed_test =
			0xFFFFFFFF - apic_read(TIMER_CURRENT_COUNT);

		// error_ratio = 1 / elapsed_time_us dalam integer tidak berguna
		// (selalu 0 karena integer division)
		// Ganti: hitung ticks per us langsung
		// elapsed_test ticks terjadi dalam elapsed_time_us us
		// ticks per us = elapsed_test / elapsed_time_us
		uint64_t ticks_per_us = 0;
		if (elapsed_time_us > 0)
			ticks_per_us = elapsed_test / elapsed_time_us;

		// Running average
		calibrated_ticks_1ns =
			(calibrated_ticks_1ns * i + ticks_per_us) / (i + 1);
	}

	LOG2_DEBUG("APIC_TIMER", "[APIC] calibrated apic timer 1ms done.");
}

// static void vxAPICTimerCalibrationUsingPIT() {
// 	outb((0x43), 0b00010100);
// 	uint16_t reload_value = (uint16_t) (1193182 / 20);
// 	outb((0X40), reload_value & 0xFF);
// 	outb((0X40), (reload_value >> 8) & 0xFF);

// 	serial_trace("PIT Reload Value : %d\n", reload_value);
// 	apic_write(TIMER_INITIAL_COUNT, 0xFFFFFFFF);
// 	apic_write(TIMER_DIVIDE_CONFIG, 0x3);
// 	apic_write(LVT_TIMER, 0x20000 | 48);

// 	uint32_t __start = apic_read(0x390);
// 	serial_trace("APIC Timer Start : %d\n", __start);

// 	uint16_t pit_status;
// 	do {
// 		outb(0x43, 0x00);
// 		pit_status = inb(0x40);
// 		pit_status |= inb(0x40) << 8;
// 	} while ((pit_status & 0x20) == 0);

// 	uint32_t __end = apic_read(0x390);
// 	serial_trace("APIC Timer End : %d\n", __end);
// 	uint32_t freq = (__start - __end) * 20;
// 	calibrated_ticks_1ns = freq;
// 	serial_trace("APIC Timer Frequency : %d\n", freq);
// }

// static void vxTSCTimerCalibration() {
// 	uint64_t tick_ns = vxHPETMinTickNs();

// 	if (!vxAPICIsTSMCSupported()) {
// 		LOG_DEBUG("APIC_TIMER", "TSMC is Not supported");
// 		return;
// 	}

// 	hpet_disable();
// 	// TODO: buat utility untuk handle ini
// 	// hpet_write(HPET_MAIN_COUNT, 0);
// 	hpet_enable();

// 	uint64_t hpet_tickss = ms2ns(1) / tick_ns;
// 	uint64_t hpet_start = vxHPETGetMainCount();
// 	uint64_t tsc_start = vxAPICReadTSC();

// 	while ((vxHPETGetMainCount() - hpet_start) < hpet_tickss)
// 		;

// 	uint64_t tsc_end = vxAPICReadTSC();

// 	// Sebelum: (tsc_end - tsc_start) / 1e6  → FPU/SSE
// 	// tsc_end - tsc_start = ticks selama 1ms
// 	// freq per ms = ticks / 1ms
// 	// bagi 1000 untuk dapat per us, bagi 1000000 untuk per ns
// 	// calibrated_tsmc_freq_1ms = ticks per ms, simpan apa adanya
// 	calibrated_tsmc_freq_1ms = (tsc_end - tsc_start);

// 	LOG_DEBUG("APIC_TIMER", "[APIC] calibrated TSC timer 1ms done.");
// }

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
	if (type != APIC_TIMER_ONE_SHOT && type != APIC_TIMER_PERIOD) {
		LOG_ERROR("APIC_TIMER", "Invalid timer type: 0x%x", type);
		return;
	}

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

	// calibrated_ticks_1ns = ticks per us (hasil kalibrasi baru)
	// count = ticks per us * interval_us
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
}

void vxAPICCreateDeadlineTimer(const uint8_t vector, const uint64_t freq_us) {
	apic_write(LVT_TIMER, (vector & 0xFF) | APIC_TIMER_DEADLINE);

	// Sebelum: calibrated_tsmc_freq_1ms * freq_us → double
	// calibrated_tsmc_freq_1ms = ticks per 1ms = ticks per 1000us
	// ticks per us = calibrated_tsmc_freq_1ms / 1000
	// tsc_delta = (calibrated_tsmc_freq_1ms / 1000) * freq_us
	// Gunakan urutan perkalian dulu untuk hindari precision loss
	const uint64_t tsc_delta = (calibrated_tsmc_freq_1ms * freq_us) / 1000;

	const uint64_t deadline = vxAPICReadTSC() + tsc_delta;
	vxWRSR(0x6E0, deadline);
}