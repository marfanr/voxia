#include "hal/apic/apic.h"
#include <hal/cpu/core.h>
#include "hal/cpu/interrupt.h"
#include "init/init.h"
#include "libk/serial.h"
#include <type.h>
#include <hal/acpi/hpet.h>
#include <hal/timer/timer.h>
#include <procc/scheduler.h>

extern void vxInitializeAPICTimer();
extern uint64_t calibrated_ticks_1us;

static boolean_t trigerred = 0;

static void usleep_backend(interrupt_stack_frame_t* _) {
	each_core_data* core = get_current_core_data();
	core->usleep_trigerred = true;
	trigerred = true;

	// if (core->core_id > 1)
	// 	LOG2_DEBUG("TIMER", "usleep backend triggerred on core %d",
	// 		   core->core_id);
}

void setup_timer_interrupt() {
	irq_register(get_current_core_cpuid(), 0xE4, (void*) usleep_backend, true, 0x28,
		     0, INTERRUPT_ATTR_KERNEL);
}

KERNEL_API void usleep(const uint64_t time_ns) {
	if (time_ns < 1000000 || !vxIsSchedulerRunning() || !vxHPETIsAvailable()) {
		if (vxHPETIsAvailable()) {
			vxHPETSleep(time_ns);
		} else {
			// Basic fallback if HPET is not available
			for (volatile uint64_t i = 0; i < time_ns / 10; i++) {
				__asm__ volatile("sti\npause");
			}
		}
		return;
	}

	uint64_t start = vxHPETGetMainCount();
	uint64_t ticks = time_ns / vxHPETMinTickNs();

	while ((vxHPETGetMainCount() - start) < ticks) {
		schedule_yield();
	}
}

INIT(Timer) {
	LOG2_INFO("TIMER", "Initializing HPET timer");

	vxInitializeAPICTimer();

	setup_timer_interrupt();
}

void init_timer_counter(time_counter_t* counter) {
	counter->current = vxHPETGetMainCount();
}

uint64_t get_timer_counter_count(time_counter_t* counter) {
	return vxHPETGetMainCount() - counter->current;
}

uint64_t get_timer_counter_count_ms(time_counter_t* counter) {
	return ns2ms((uint64_t) (vxHPETGetMainCount() - counter->current)
		     * vxHPETMinTickNs());
}

uint64_t get_timer_counter_count_ns(time_counter_t* counter) {
	return (get_timer_counter_count(counter) * vxHPETMinTickNs());
}

