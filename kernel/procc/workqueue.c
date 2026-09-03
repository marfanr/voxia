#include "procc/workqueue.h"
#include "hal/acpi/acpi.h"
#include "hal/cpu/cpuid.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "memory/slab.h"
#include "procc/process.h"
#include "procc/thread.h"
#include <hal/cpu/core.h>
#include <spinlock.h>
#include <str.h>
#include <type.h>
#include <vector.h>

static struct slab_cache* workqueue_cache;

#define SLOT_EMPTY 0x00
#define SLOT_BUSY 0xFF

static void workqueue_process() {
	each_core_data* core = get_current_core_data();
	LOG2_INFO("workqueue", "worker thread running on core %d",
	          core->core_id);

	while (true) {

		if (!__atomic_load_n(&core->workqueue_count,
		                     __ATOMIC_ACQUIRE)) {
			thread_sleep(10);
			continue;
		}

		boolean_t any_processed = false;

		spin_acquire(&core->wq_lock);
		workqueue_t* workqueue = core->wq_head;
		spin_release(&core->wq_lock);

		while (workqueue != nullptr) {
			if (__atomic_load_n(&workqueue->in_use,
			                    __ATOMIC_ACQUIRE) != SLOT_BUSY) {
				spin_acquire(&core->wq_lock);
				workqueue = workqueue->next;
				spin_release(&core->wq_lock);
				continue;
			}

			if (workqueue->dependency) {
				boolean_t dependency_done = true;

				for (size_t j = 0;
				     j < workqueue->dependency->size; j++) {
					workqueue_t* dep =
					    workqueue->dependency->data[j];

					if (!dep) {
						continue;
					}

					if (__atomic_load_n(&dep->in_use,
					                    __ATOMIC_ACQUIRE) !=
					    SLOT_FINISHED) {

						dependency_done = false;
						break;
					}
				}

				if (!dependency_done) {
					spin_acquire(&core->wq_lock);
					workqueue = workqueue->next;
					spin_release(&core->wq_lock);
					continue;
				}
			}

			any_processed = true;
			if (workqueue->function) {
				((void (*)(void*))workqueue->function)(
				    workqueue->data);
			} else {
				LOG2_ERROR("workqueue",
				           "core %d slot function pointer "
				           "null, skipping",
				           core->core_id);
			}

			if (workqueue->dependency) {
				for (size_t j = 0;
				     j < workqueue->dependency->size; j++) {
					workqueue_t* dep =
					    workqueue->dependency->data[j];
					if (dep) {
						uint32_t current_ref =
						    __atomic_fetch_sub(
						        &dep->refcount, 1,
						        __ATOMIC_RELEASE);
						if (current_ref == 1) {
							if (__atomic_load_n(
							        &dep->in_use,
							        __ATOMIC_ACQUIRE) ==
							    SLOT_FINISHED) {
								__atomic_store_n(
								    &dep->in_use,
								    SLOT_EMPTY,
								    __ATOMIC_RELEASE);
							}
						}
					}
				}
				vector_destroy(workqueue->dependency);
				kfree2(workqueue->dependency);
				workqueue->dependency = NULL;
			}

			uint32_t self_ref = __atomic_fetch_sub(
			    &workqueue->refcount, 1, __ATOMIC_RELEASE);
			if (self_ref == 1) {
				__atomic_store_n(&workqueue->in_use, SLOT_EMPTY,
				                 __ATOMIC_RELEASE);
			} else {
				__atomic_store_n(&workqueue->in_use,
				                 SLOT_FINISHED,
				                 __ATOMIC_RELEASE);
			}

			__atomic_fetch_sub(&core->workqueue_count, 1,
			                   __ATOMIC_RELEASE);

			spin_acquire(&core->wq_lock);
			workqueue_t* next_node = workqueue->next;
			spin_release(&core->wq_lock);
			workqueue = next_node;
		}

		// Cleanup pass for fully completed tasks (refcount == 0)
		spin_acquire(&core->wq_lock);
		workqueue_t* current = core->wq_head;
		while (current != nullptr) {
			workqueue_t* next_node = current->next;
			if (__atomic_load_n(&current->in_use,
			                    __ATOMIC_ACQUIRE) == SLOT_EMPTY &&
			    __atomic_load_n(&current->refcount,
			                    __ATOMIC_ACQUIRE) == 0) {

				if (current->prev) {
					current->prev->next = current->next;
				} else {
					core->wq_head = current->next;
				}

				if (current->next) {
					current->next->prev = current->prev;
				} else {
					core->wq_tail = current->prev;
				}

				slab_free(workqueue_cache, current);
			}
			current = next_node;
		}
		spin_release(&core->wq_lock);

		if (!any_processed) {
			schedule_yield();
		}
	}

	thread_exit();
}

KERNEL_API workqueue_t* vxAddWorkqueueTask(void (*task)(void*), void* arg,
                                vector(workqueue_ptr_t) * dependency) {
	static uint8_t next_core_hint = 1;

	uint8_t jum_core = vxGetNumberOfCores();
	if (jum_core <= 1) {
		LOG_ERROR("workqueue", "no worker core available");
		return 0;
	}

	for (uint8_t attempt = 0; attempt < jum_core; attempt++) {
		// Dapatkan index CPU secara linear round-robin (skip BSP di
		// index 0)
		uint8_t core_idx = ((__atomic_fetch_add(&next_core_hint, 1,
		                                        __ATOMIC_RELAXED)) %
		                    (jum_core - 1)) +
		                   1;

		auto cpu_info = vxGetCpuInfoByIndex(core_idx);
		if (!cpu_info || cpu_info->status != Active) {
			continue;
		}

		// Temukan APIC ID dari index yang valid
		uint8_t target_apic_id = cpu_info->apicid;

		each_core_data* core = vxGetCoreDataByCoreID(target_apic_id);

		workqueue_t* new_node = vxSlabAlloc(workqueue_cache);
		memset(new_node, 0, sizeof(workqueue_t));

		new_node->function = (void*)task;
		new_node->data = arg;
		new_node->dependency = dependency;
		new_node->refcount = 2; // 1 for worker, 1 for caller
		new_node->in_use = SLOT_BUSY;

		spin_acquire(&core->wq_lock);
		if (!core->wq_head) {
			core->wq_head = new_node;
			core->wq_tail = new_node;
		} else {
			core->wq_tail->next = new_node;
			new_node->prev = core->wq_tail;
			core->wq_tail = new_node;
		}
		spin_release(&core->wq_lock);

		__atomic_fetch_add(&core->workqueue_count, 1, __ATOMIC_RELEASE);
		return new_node;
	}

	LOG_ERROR("workqueue", "no active worker core found");
	return 0;
}

INIT(Workqueue) {
	vxCreateSlabCache(&workqueue_cache, "workqueue", sizeof(workqueue_t), 0,
	                  0);

	uint8_t bsp_apic_id = (uint8_t)cpuid_get_bsp_id();

	auto jum_core = vxGetNumberOfCores();
	for (uint8_t i = 0; i < jum_core; i++) {
		auto cpu_info = vxGetCpuInfoByIndex(i);
		if (!cpu_info)
			continue;

		uint8_t apic_id = cpu_info->apicid;
		if (apic_id == bsp_apic_id)
			continue;

		serial2_printf("workqueue init on core %d\n", cpu_info->cpuid);

		each_core_data* core = vxGetCoreDataByCoreID(apic_id);
		core->wq_lock = (spinlock_t){0};
		core->wq_head = nullptr;
		core->wq_tail = nullptr;

		auto thr =
		    create_thread((uintptr_t)workqueue_process, 0, 0, i, 1, 0);

		attach_to_scheduler(thr);
	}
}