#include "hal/cpu/core.h"
#include "hal/cpu/cpuid.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "hal/timer/timer.h"
#include "init/init.h"
#include "libk/io.h"
#include "libk/serial.h"
#include <hal/acpi/hpet.h>
#include <hal/apic/apic.h>

static double calibrated_ticks_1ns = 0;
static double calibrated_tsmc_freq_1ms = 0;
static boolean_t tsmc_calibrated = 0;

void timer_handle(interrupt_stack_frame_t* _) {
	LOG_INFO("TIMER", "timer trigger");
}

static uint64_t vxAPICReadTSC() {
	uint32_t eax, edx;
	__asm__ volatile("rdtsc" : "=a"(eax), "=d"(edx));
	return ((uint64_t)edx << 32) | eax;
}

static void calibrate_tsmc_backend(interrupt_stack_frame_t* _) {
	tsmc_calibrated = 1;
}

boolean_t vxAPICIsTSMCSupported() {
	uint32_t eax, ebx, ecx, edx;
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	return (ecx & (1 << 24)) != 0;
}

bool vxTSChasInvariant(void) {
	uint32_t edx, unused;
	cpuid(0x80000007, 0, &unused, &unused, &unused, &edx);
	return edx & (1 << 8); // bit 8 = Invariant TSC
}

static void vxAPICTimerCalibrationUsingHPET() {
	double tick_ns = vxHPETMinTickNs();
	for (int i = 0; i < 5; i++) {
		uint64_t hpet_ticks =
		    us2ns(1) / tick_ns; // Konversi 1ms ke nanodetik

		apic_write(TIMER_DIVIDE_CONFIG, 0b1011);

		hpet_disable();
		hpet_write(HPET_MAIN_COUNT, 0);
		hpet_enable();

		time_counter_t counter = {0};
		vxTimerCounterInit(&counter);

		// validate
		apic_write(LVT_TIMER, 0x20 | APIC_TIMER_ONE_SHOT);
		apic_write(TIMER_INITIAL_COUNT, 0xFFFFFFFF);
		while ((vxTimerCounterCount(&counter)) < 1e3)
			;
		double elapsed_time_us =
		    vxTimerCounterCountInNs(&counter) * 1e-3;

		// correction
		double elapsed_test =
		    0xFFFFFFFF - apic_read(TIMER_CURRENT_COUNT);

		double error_ratio = 1.0f / elapsed_time_us;
		double elapsed = elapsed_test * error_ratio;

		calibrated_ticks_1ns =
		    calibrated_ticks_1ns * i + (double)elapsed;
		calibrated_ticks_1ns /= (i + 1);
	}

	LOG2_DEBUG("APIC_TIMER", "[APIC] calibrated apic timer 1ms done.");
}

static void vxAPICTimerCalibrationUsingPIT() {
	outb((0x43), 0b00010100);
	uint16_t reload_value = (uint16_t)(1193182 / 20);
	outb((0X40), reload_value & 0xFF);        // LSB
	outb((0X40), (reload_value >> 8) & 0xFF); // MSB

	serial_trace("PIT Reload Value : %d\n", reload_value);
	apic_write(TIMER_INITIAL_COUNT, 0xFFFFFFFF);
	apic_write(TIMER_DIVIDE_CONFIG, 0x3);
	apic_write(LVT_TIMER, 0x20000 | 48); // Periodic mode
	// Set the initial count

	uint32_t __start = apic_read(0x390);
	serial_trace("APIC Timer Start : %d\n", __start);
	// menunggu pit
	uint16_t pit_status;
	do {
		outb(0x43, 0x00);
		pit_status = inb(0x40); // Baca status PIT
		pit_status |= inb(0x40) << 8;
	} while ((pit_status & 0x20) == 0);

	uint32_t __end = apic_read(0x390);
	serial_trace("APIC Timer End : %d\n", __end);
	uint32_t freq = (__start - __end) * 20;
	calibrated_ticks_1ns = freq;
	serial_trace("APIC Timer Frequency : %d\n", freq);
}
static void vxTSCTimerCalibration() {
	double tick_ns = vxHPETMinTickNs();

	if (!vxAPICIsTSMCSupported()) {
		LOG_DEBUG("APIC_TIMER", "TSMC is Not supported");
		return;
	}

	hpet_disable();
	hpet_write(HPET_MAIN_COUNT, 0);
	hpet_enable();

	uint64_t hpet_tickss = ms2ns(1) / tick_ns;
	uint64_t hpet_start = vxHPETGetMainCount();
	uint64_t tsc_start = vxAPICReadTSC();

	while ((vxHPETGetMainCount() - hpet_start) < hpet_tickss)
		;

	uint64_t tsc_end = vxAPICReadTSC();

	calibrated_tsmc_freq_1ms = (double)(tsc_end - tsc_start) / 1e6f;

	LOG_DEBUG("APIC_TIMER", "[APIC] calibrated TSC timer 1ms done.");
}

void vxInitializeAPICTimer() {
	// if (vxHPETIsAvailable()) {
	vxAPICTimerCalibrationUsingHPET();
	// } else {
	// 	vxAPICTimerCalibrationUsingPIT();
	// }

	// vxTSCTimerCalibration();
}

#define APIC_TIMER_MASKED 0x80

void vxAPICCreateTimer(uint32_t type, double freq_us, uint8_t vector) {
	apic_write(TIMER_DIVIDE_CONFIG, 0b1011);
	// Validasi parameter inpu
	if (type != APIC_TIMER_ONE_SHOT && type != APIC_TIMER_PERIOD) {
		LOG_ERROR("APIC_TIMER", "Invalid timer type: 0x%x", type);
		return;
	}

	if (vector < 0x10 || vector > 0xFE) {
		LOG_ERROR("APIC_TIMER", "Invalid interrupt vector: 0x%x",
		          vector);
		return;
	}

	if (freq_us == 0) {
		LOG_ERROR("APIC_TIMER", "Frequency cannot be zero");
		return;
	}

	if (calibrated_ticks_1ns == 0) {
		LOG_ERROR("APIC_TIMER", "APIC timer not calibrated");
		return;
	}

	// Konfigurasi LVT Timer
	uint32_t lvt_value = (vector & 0xFF) | type | APIC_TIMER_MASKED;
	apic_write(LVT_TIMER, lvt_value);

	// Hitung initial count berdasarkan frekuensi
	uint32_t initial_count;

	if (type == APIC_TIMER_ONE_SHOT) {
		// Untuk one-shot: hitung ticks untuk interval yang diminta
		initial_count = calibrated_ticks_1ns * freq_us;

		// Pastikan tidak melebihi batas maksimum (32-bit)
		if (initial_count > 0xFFFFFFFF) {
			LOG_WARN("APIC_TIMER",
			         "Initial count truncated to 32-bit");
			initial_count = 0xFFFFFFFF;
		}
	} else // APIC_TIMER_PERIODIC
	{
		// Untuk periodic: initial count juga berfungsi sebagai reload
		// value
		initial_count = calibrated_ticks_1ns * freq_us;

		if (initial_count > 0xFFFFFFFF) {
			LOG_WARN("APIC_TIMER",
			         "Period too long, truncated to 32-bit");
			initial_count = 0xFFFFFFFF;
		}
	}

	// Unmask timer dan tulis konfigurasi
	lvt_value &= ~APIC_TIMER_MASKED; // Unmask timer
	apic_write(LVT_TIMER, lvt_value);
	// apic_write(TIMER_DIVIDE_CONFIG, 0x3);

	// Set initial count untuk memulai timer
	apic_write(TIMER_INITIAL_COUNT, initial_count);
}

void vxAPICCreateDeadlineTimer(const uint8_t vector, const double freq_us) {
	apic_write(LVT_TIMER, (vector & 0xFF) | APIC_TIMER_DEADLINE);
	// Kalkulasi langsung dalam double untuk presisi maksimal
	const double tsc_delta_d = (calibrated_tsmc_freq_1ms * freq_us);
	const uint64_t tsc_delta = (uint64_t)tsc_delta_d;
	const uint64_t deadline = vxAPICReadTSC() + tsc_delta;

	vxWRSR(0x6E0, deadline);
}