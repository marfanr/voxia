#include "procc/workqueue.h"
#include "hal/acpi/acpi.h"
#include "hal/acpi/hpet.h"
#include "hal/cpu/core.h"
#include "hal/cpu/spinlock.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "libk/serial.h"
#include <type.h>
#include <vector.h>
#include "procc/thread.h"

static void workqueue_process() {
	each_core_data* core = vxGetCoreData();
	LOG2_INFO("workqueue", "worker thread running on core %d",
		  core->core_id);
	while (true) {
		if (!__atomic_load_n(&core->workqueue_count,
				     __ATOMIC_ACQUIRE)) {
			__asm__ volatile("pause");
			continue;
		}

		for (uint16_t i = 0; i < VOXIA_MAX_WORKQUEUE_EACH_CORE; i++) {
			auto workqueue = &core->workqueue[i];

			if (!__atomic_load_n(&workqueue->in_use,
					     __ATOMIC_ACQUIRE))
				continue;

			if (workqueue->dependency) {
				boolean_t dependency_done = true;

				for (size_t j = 0;
				     j < workqueue->dependency->size; j++) {
					auto dep =
						workqueue->dependency->data[j];

					if (!dep) {
						__atomic_store_n(
							&workqueue->in_use,
							false,
							__ATOMIC_RELEASE);
						__atomic_fetch_sub(
							&core->workqueue_count,
							1, __ATOMIC_RELEASE);
						dependency_done = false;
						// goto workqueue_skip;
						break;
					}

					if (__atomic_load_n(&dep->in_use,
							    __ATOMIC_ACQUIRE)) {
						dependency_done = false;
						// goto workqueue_skip;
						break;
					}
				}
				if (!dependency_done)
					continue;
			}

			((void (*)()) core->workqueue[i].function)();

			__atomic_store_n(&core->workqueue[i].in_use, false,
					 __ATOMIC_RELEASE);
			__atomic_fetch_sub(&core->workqueue_count, 1,
					   __ATOMIC_RELEASE);
		}
	}
	vxThreadExit();
}

static spinlock_t lock;

#define SLOT_EMPTY 0x00
#define SLOT_BUSY 0xFF

workqueue_t* vxAddWorkqueueTask(void (*task)(void*), void* arg,
				vector(workqueue_ptr_t) * dependency) {
	static int next_core_hint = 1;

	// Ambil core secara bergiliran
	uint16_t core_count = vxGetNumberOfCores();
	int start_index =
		__atomic_fetch_add(&next_core_hint, 1, __ATOMIC_RELAXED)
		% core_count;

	if (start_index == 0) {
		// Jangan kirim ke core 0, karena biasanya core 0 juga menjalankan
		// scheduler dan bisa menyebabkan deadlock jika worker thread menunggu
		// task lain yang dijalankan di core 0.
		start_index = 1;
	}

	each_core_data* core = vxGetCoreDataByCoreID(start_index);
	spin_acquire(&lock);

	if (core == 0) {
		LOG2_ERROR("workqueue", "error null core");
		return 0;
	}

	LOG2_INFO("workqueue", "adding task to core %d (0x%x), num work %d",
		  core->core_id, core, core->workqueue_count);

	for (uint16_t i = 0; i < VOXIA_MAX_WORKQUEUE_EACH_CORE; i++) {
		uint8_t expected = SLOT_EMPTY;

		/* 
 * CAS (Compare and Swap):
 * Jika in_use == 0x00, maka set ke 0xFF secara atomik dan kembalikan true.
 * Jika in_use sudah 0xFF, maka kembalikan false (berarti slot sudah diambil orang lain).
 */
		if (__atomic_compare_exchange_n(
			    &core->workqueue[i].in_use, &expected, SLOT_BUSY,
			    false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {

			/* Sekarang Anda pemilik sah slot 'i'. 
     * Tidak akan ada thread lain yang bisa masuk ke blok ini untuk index yang sama. */

			core->workqueue[i].function = task;
			core->workqueue[i].data = arg;
			core->workqueue[i].dependency = dependency;

			/* Gunakan Release Barrier agar penulisan data di atas 
     * dipastikan selesai sebelum thread lain (consumer) memprosesnya. */
			__atomic_fetch_add(&core->workqueue_count, 1,
					   __ATOMIC_RELEASE);

			LOG2_INFO("workqueue",
				  "task added to core %d slot %d (0x%x), num "
				  "work %d\n",
				  core->core_id, i, &core->workqueue[i],
				  core->workqueue_count);

			spin_release(&lock);
			return &core->workqueue[i];
		}
	}

	LOG_ERROR("workqueue", "no available slot in workqueue on core %d",
		  core->core_id);
	spin_release(&lock);
	return 0;
}

INIT(Workqueue) {
	for (uint16_t i = 1; i < vxGetActiveCoreCount(); i++) {
		auto cpu_info = vxGetCpuInfo(i);
		if (cpu_info->status != Active) {
			serial2_printf("cpu %d not active\n", cpu_info->cpuid);
			continue;
		}
		serial2_printf("workqueue init on core %d\n", cpu_info->cpuid);
		vxCreateThread((uintptr_t) workqueue_process, i, 1, 0);
	}
}