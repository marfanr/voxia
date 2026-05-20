#ifndef __HAL__CPU__CORE_H__
#define __HAL__CPU__CORE_H__

#include "autoconf.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include "procc/workqueue.h"
#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct  __attribute__((aligned(64))) {
	uint64_t canary;
	uint8_t core_id;
	boolean_t usleep_trigerred;
	scheduler_core_t* scheduler;
	uint32_t workqueue_count;
	thread_t* active_thread;

	uint8_t _pad[24];
	// 1 cache line end

	workqueue_t workqueue[VOXIA_MAX_WORKQUEUE_EACH_CORE];
} each_core_data;

void update_core_gs(uint8_t id);
uint8_t get_current_core_cpuid();
each_core_data* vxGetCoreData();
each_core_data* vxGetCoreDataByCoreID(uint8_t core_id);
uint8_t vxGetActiveCoreCount();

#ifdef __cplusplus
}
#endif

#endif // __HAL__CPU__CORE_H__