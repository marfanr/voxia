#include "hal/rand/rand.h"
#include "hal/acpi/hpet.h"
#include "hal/cpu/cpuid.h"
#include "init/init.h"
#include "libk/serial.h"

static boolean_t random_available = 0;

#define RAND_MAX 0xFFFFFFFF

INIT(Rand) {
	uint32_t ecx, unused;
	cpuid(1, 0, &unused, &unused, &ecx, &unused);
	random_available = (ecx >> 30) & 1;
	LOG_INFO("Rand ", "random is available %d", random_available);
}

uint32_t vxRand() {
	if (random_available) {
		uint32_t rand = 0;
		asm volatile("rdrand %0" : "=r"(rand));
		return rand & RAND_MAX;
	} else if (vxHPETIsAvailable()) {
		// fallback using HPET if available
		uint32_t rand = vxHPETGetMainCount() & RAND_MAX;
		return rand;
	}
	return 0;
}