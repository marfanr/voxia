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

					/*
                     * FIX: dependency NULL bukan berarti slot harus di-free.
                     * Ini adalah data dependency yang tidak valid / sudah
                     * selesai sebelum dicatat. Anggap sebagai "sudah selesai"
                     * sehingga tidak memblokir eksekusi.
                     *
                     * Jika memang NULL dependency harus membatalkan task,
                     * set dependency_invalid = true lalu break.
                     */
					if (!dep) {
						/* Dep sudah selesai / tidak ada, lanjutkan cek berikutnya */
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

	uint8_t core_count = vxGetNumberOfCores();
	if (core_count <= 1) {
		LOG2_ERROR("workqueue",
			   "no worker cores available (only core 0)");
		return 0;
	}

	uint8_t worker_count = core_count - 1; /* core 0 bukan worker */
	uint8_t start_index =
		(__atomic_fetch_add(&next_core_hint, 1, __ATOMIC_RELAXED)
		 % worker_count)
		+ 1;

	for (uint8_t attempt = 0; attempt < worker_count; attempt++) {
		uint8_t target_index =
			((start_index - 1 + attempt) % worker_count) + 1;

		each_core_data* core = vxGetCoreDataByCoreID(target_index);
		if (!core) {
			LOG2_WARN("workqueue",
				  "core %d returned null data, skipping",
				  target_index);
			continue;
		}

		/* Cepat-cek apakah masih ada slot kosong sebelum ambil lock */
		if (__atomic_load_n(&core->workqueue_count, __ATOMIC_ACQUIRE)
		    >= VOXIA_MAX_WORKQUEUE_EACH_CORE) {
			continue; /* core ini penuh, coba yang lain */
		}

		spin_acquire(&lock);

		LOG2_INFO(
			"workqueue",
			"adding task to core %d (0x%x), current work count %d",
			core->core_id, (uintptr_t) core, core->workqueue_count);

		for (uint16_t i = 0; i < VOXIA_MAX_WORKQUEUE_EACH_CORE; i++) {
			uint8_t expected = SLOT_EMPTY;

			if (__atomic_compare_exchange_n(
				    &core->workqueue[i].in_use, &expected,
				    SLOT_BUSY, false, __ATOMIC_ACQUIRE,
				    __ATOMIC_RELAXED)) {

				core->workqueue[i].function = (void*) task;
				core->workqueue[i].data = arg;
				core->workqueue[i].dependency = dependency;

				/* Pastikan data tertulis sebelum counter diincrement */
				__atomic_fetch_add(&core->workqueue_count, 1,
						   __ATOMIC_RELEASE);

				LOG2_INFO("workqueue",
					  "task added to core %d slot %d "
					  "(0x%x), total %d",
					  core->core_id, i,
					  (uintptr_t) &core->workqueue[i],
					  core->workqueue_count);

				spin_release(&lock);
				return &core->workqueue[i];
			}
		}

		/* Slot penuh setelah lock, coba core berikutnya */
		LOG2_WARN("workqueue",
			  "no slot available on core %d, trying next core",
			  core->core_id);
		spin_release(&lock);
	}

	LOG_ERROR("workqueue",
		  "no available slot in any worker core (all %d cores full)",
		  worker_count);
	return 0;
}

INIT(Workqueue) {
	for (uint8_t i = 1; i < vxGetActiveCoreCount(); i++) {
		auto cpu_info = vxGetCpuInfo(i);
		if (cpu_info->status != Active) {
			serial2_printf("cpu %d not active\n", cpu_info->cpuid);
			continue;
		}
		serial2_printf("workqueue init on core %d\n", cpu_info->cpuid);
		vxCreateThread((uintptr_t) workqueue_process, i, 1, 0);
	}
}