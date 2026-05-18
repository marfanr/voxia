#include "procc/workqueue.h"
#include "hal/acpi/acpi.h"
#include "hal/acpi/hpet.h"
#include "hal/cpu/core.h"
#include <spinlock.h>
#include "init/init.h"
#include "libk/debug/debug.h"
#include "libk/serial.h"
#include <type.h>
#include <vector.h>
#include "procc/thread.h"

static spinlock_t lock;

#define SLOT_EMPTY 0x00
#define SLOT_BUSY 0xFF

static void workqueue_process() {
	each_core_data* core = vxGetCoreData();
	LOG2_INFO("workqueue", "worker thread running on core %d",
		  core->core_id);

	while (true) {
		/* Spin jika tidak ada pekerjaan */
		if (!__atomic_load_n(&core->workqueue_count,
				     __ATOMIC_ACQUIRE)) {
			__asm__ volatile("pause");
			continue;
		}

		for (uint16_t i = 0; i < VOXIA_MAX_WORKQUEUE_EACH_CORE; i++) {
			workqueue_t* workqueue = &core->workqueue[i];

			/* Skip slot yang tidak aktif */
			if (!__atomic_load_n(&workqueue->in_use,
					     __ATOMIC_ACQUIRE))
				continue;

			/* --- Cek dependency --- */
			if (workqueue->dependency) {
				boolean_t dependency_done = true;
				boolean_t dependency_invalid = false;

				for (size_t j = 0;
				     j < workqueue->dependency->size; j++) {
					workqueue_t* dep =
						workqueue->dependency->data[j];

					if (!dep) {
						continue;
					}

					if (__atomic_load_n(&dep->in_use,
							    __ATOMIC_ACQUIRE)) {
						/* Dep masih berjalan, tunda task ini */
						dependency_done = false;
						break;
					}
				}

				(void) dependency_invalid; /* suppress unused warning */

				if (!dependency_done)
					continue;
			}

			/* --- Jalankan task --- */
			if (workqueue->function) {
				((void (*)(void*)) workqueue->function)(
					workqueue->data);
			} else {
				LOG2_ERROR("workqueue",
					   "core %d slot %d: function pointer "
					   "null, skipping",
					   core->core_id, i);
			}

			/* Tandai slot sebagai selesai */
			__atomic_store_n(&workqueue->in_use, SLOT_EMPTY,
					 __ATOMIC_RELEASE);
			__atomic_fetch_sub(&core->workqueue_count, 1,
					   __ATOMIC_RELEASE);
		}
	}

	vxThreadExit();
}

workqueue_t* vxAddWorkqueueTask(void (*task)(void*), void* arg,
				vector(workqueue_ptr_t) * dependency) {
	static uint8_t next_core_hint = 1;

	// Coba cari slot kosong di core yang aktif (selain core 0)
	// Kita iterasi semua kemungkinan APIC ID untuk fallback yang robust
	for (uint8_t attempt = 0; attempt < VOXIA_MAX_CORE; attempt++) {
		uint8_t target_apic_id =
			((__atomic_fetch_add(&next_core_hint, 1, __ATOMIC_RELAXED))
			 % (VOXIA_MAX_CORE - 1))
			+ 1;

		auto cpu_info = vxGetCpuInfo(target_apic_id);
		if (!cpu_info || cpu_info->status != Active) {
			continue; // Core tidak ada atau tidak aktif, cari yang lain
		}

		each_core_data* core = vxGetCoreDataByCoreID(target_apic_id);

		/* Cepat-cek apakah masih ada slot kosong sebelum ambil lock */
		if (__atomic_load_n(&core->workqueue_count, __ATOMIC_ACQUIRE)
		    >= VOXIA_MAX_WORKQUEUE_EACH_CORE) {
			continue; /* core ini penuh, coba yang lain */
		}

		spin_acquire(&lock);

		// Re-check after lock
		if (core->workqueue_count >= VOXIA_MAX_WORKQUEUE_EACH_CORE) {
			spin_release(&lock);
			continue;
		}

		for (uint16_t i = 0; i < VOXIA_MAX_WORKQUEUE_EACH_CORE; i++) {
			uint8_t expected = SLOT_EMPTY;

			if (__atomic_compare_exchange_n(
				    &core->workqueue[i].in_use, &expected,
				    SLOT_BUSY, false, __ATOMIC_ACQUIRE,
				    __ATOMIC_RELAXED)) {

				core->workqueue[i].function = (void*) task;
				core->workqueue[i].data = arg;
				core->workqueue[i].dependency = dependency;

				__atomic_fetch_add(&core->workqueue_count, 1,
						   __ATOMIC_RELEASE);

				spin_release(&lock);
				return &core->workqueue[i];
			}
		}

		spin_release(&lock);
	}

	LOG_ERROR("workqueue", "no available slot in any active worker core");
	return 0;
}

INIT(Workqueue) {
	for (uint8_t i = 1; i < vxGetNumberOfCores(); i++) {
		auto cpu_info = vxGetCpuInfo(i);
		if (cpu_info->status != Active) {
			serial2_printf("cpu %d not active\n", cpu_info->cpuid);
			continue;
		}
		serial2_printf("workqueue init on core %d\n", cpu_info->cpuid);
		vxCreateThread((uintptr_t) workqueue_process, i, 1, 0);
	}
}