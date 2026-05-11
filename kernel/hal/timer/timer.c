#include "hal/apic/apic.h"
#include "hal/cpu/core.h"
#include "hal/cpu/interrupt.h"
#include "init/init.h"
#include "libk/serial.h"
#include "libk/type.h"
#include <hal/acpi/hpet.h>
#include <hal/timer/timer.h>

extern void vxInitializeAPICTimer();
extern uint64_t calibrated_ticks_1us;

static boolean_t trigerred = 0;

void usleep_backend(interrupt_stack_frame_t* _) {
	each_core_data* core = vxGetCoreData();
	core->usleep_trigerred = true;
	trigerred = true;

	if (core->core_id > 1)
		LOG2_DEBUG("TIMER", "usleep backend triggerred on core %d",
			   core->core_id);
}

void usleep(const double time_ns) {
	if (time_ns < 1e6 && vxHPETIsAvailable()) {
		vxHPETSleep((uint64_t) (time_ns));
		return;
	}

	each_core_data* core = vxGetCoreData();
	core->usleep_trigerred = false;

	vxAPICCreateTimer(APIC_TIMER_ONE_SHOT, time_ns / 1e3, 0x24);

	while (!core->usleep_trigerred)
		__asm__ volatile("pause");
}

INIT(Timer) {
	LOG2_INFO("TIMER", "Initializing HPET timer");

	vxInitializeAPICTimer();
	// LOG_INFO("TIMER", "APIC timer initialized");
	irq_register(coreGetCpuID(), 0x24, (void*) usleep_backend, true, 0x28,
		     0, INTERRUPT_ATTR_KERNEL);

	// LOG_DEBUG("TIMER", "uslep test 5us");
	// usleep(5);
	// LOG_DEBUG("TIMER", "uslep test done");
}

void vxTimerCounterInit(time_counter_t* counter) {
	counter->current = vxHPETGetMainCount();
}

uint64_t vxTimerCounterCount(time_counter_t* counter) {
	return vxHPETGetMainCount() - counter->current;
}

double vxTimerCounterCountInMs(time_counter_t* counter) {
	return ns2ms((double) (vxHPETGetMainCount() - counter->current)
		     * vxHPETMinTickNs());
}

double vxTimerCounterCountInNs(time_counter_t* counter) {
	return (vxTimerCounterCount(counter) * vxHPETMinTickNs());
}
