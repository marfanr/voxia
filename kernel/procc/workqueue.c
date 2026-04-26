#include "procc/workqueue.h"
#include "hal/acpi/acpi.h"
#include "hal/acpi/hpet.h"
#include "hal/cpu/core.h"
#include "hal/cpu/spinlock.h"
#include "init/init.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "libk/vector.h"
#include "procc/thread.h"

static void workqueue_process() {
	each_core_data* core = vxGetCoreData();
	LOG2_INFO("workqueue", "worker thread running on core %d",
	          core->core_id);
	while (true) {
		if (!core->workqueue_count) {
			__asm__ volatile("pause");
			continue;
		}

		// TODO: optimize this
		for (uint16_t i = 0; i < VOXIA_MAX_WORKQUEUE_EACH_CORE; i++) {
			// check dependency
			auto workqueue = &core->workqueue[i];
			if (!workqueue->in_use)
				continue;

			// LOG_DEBUG("WORKQUEUE", "checking workqueue task in
			// slot %d", i);
			if (workqueue->dependency) {
				boolean_t dependency_done = true;
				// LOG_DEBUG("WORKQUEUE", "workqueue task in
				// slot %d has dependency size %d", i,
				//           workqueue->dependency->size);

				for (size_t j = 0;
				     j < workqueue->dependency->size; j++) {
					auto dep =
					    workqueue->dependency->data[j];
					if (!dep) {
						LOG_ERROR(
						    "WORKQUEUE",
						    "dependency %d is null", j);
						return;
					}

					// LOG_DEBUG("WORKQUEUE", "checking
					// dependency %d", j);
					if (dep->in_use) {
						dependency_done = false;
						break;
					}
				}
				if (!dependency_done)
					continue;
			}

			// LOG_INFO("workqueue", "core %d executing task in slot
			// %d", core->core_id, i);
			// core->workqueue[i].function(core->workqueue[i].data);
			((void (*)())core->workqueue[i].function)();

			core->workqueue[i].in_use = false;
			core->workqueue_count -= 1;
		}
	}
	vxThreadExit();
}

static spinlock_t lock;

workqueue_t* vxAddWorkqueueTask(void (*task)(void*), void* arg,
                                vector(workqueue_ptr_t) * dependency) {
	// find core with lower workqueue count
	each_core_data* core = vxGetCoreDataByCoreID(1);
	LOG2_INFO("workqueue", "adding task to core %d", core->core_id);
	spin_acquire(&lock);
	uint16_t core_count = vxGetNumberOfCores();

	// avoid bsp for now (core 0)
	for (uint16_t i = 1; i < core_count; i++) {
		each_core_data* other_core = vxGetCoreDataByCoreID(i);
		if (other_core->workqueue_count < core->workqueue_count)
			core = other_core;
	}

	for (uint16_t i = 0; i < VOXIA_MAX_WORKQUEUE_EACH_CORE; i++) {
		if (!core->workqueue[i].in_use) {
			core->workqueue[i].function = task;
			core->workqueue[i].data = arg;
			core->workqueue[i].in_use = true;
			if (dependency)
				core->workqueue[i].dependency = dependency;
			core->workqueue_count += 1;
			LOG2_INFO("workqueue", "task added to core %d slot %d",
			          core->core_id, i);
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
		if (cpu_info->status != Active)
			continue;

		serial2_printf("workqueue init on core %d\n", cpu_info->cpuid);
		vxCreateThread((uintptr_t)workqueue_process, i, 1, 0);
	}
	// vxCreateThread((uintptr_t)workqueue_process, 2, 1, 0);
}