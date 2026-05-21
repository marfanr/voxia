#include "hal/apic/apic.h"
#include <hal/cpu/core.h>
#include "hal/cpu/interrupt.h"
#include "init/init.h"
#include "libk/serial.h"
#include <type.h>
#include <hal/acpi/hpet.h>
#include <hal/timer/timer.h>

extern void vxInitializeAPICTimer();
extern uint64_t calibrated_ticks_1us;
extern boolean_t g__scheduler__is__running;

static boolean_t trigerred = 0;

static void usleep_backend(interrupt_stack_frame_t* _) {
	each_core_data* core = vxGetCoreData();
	core->usleep_trigerred = true;
	trigerred = true;

	if (core->core_id > 1)
		LOG2_DEBUG("TIMER", "usleep backend triggerred on core %d",
			   core->core_id);
}

void setup_timer_interrupt() {
	irq_register(get_current_core_cpuid(), 0x24, (void*) usleep_backend, true, 0x28,
		     0, INTERRUPT_ATTR_KERNEL);
}

void usleep(const uint64_t time_ns) {
	if (time_ns < 1000000 || !g__scheduler__is__running) {
		if (vxHPETIsAvailable()) {
			vxHPETSleep(time_ns);
			return;
		}
	}

	each_core_data* core = vxGetCoreData();
	core->usleep_trigerred = false;

	vxAPICCreateTimer(APIC_TIMER_ONE_SHOT, time_ns / 1000, 0x24);

	while (!core->usleep_trigerred)
		__asm__ volatile("pause");
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
