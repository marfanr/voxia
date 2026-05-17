#ifndef __HAL__CPU__CORE_H__
#define __HAL__CPU__CORE_H__

#include "autoconf.h"
#include "libk/type.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include "procc/workqueue.h"

typedef struct {
	uint64_t canary;
	uint8_t core_id;
	boolean_t usleep_trigerred;
	scheduler_core_t* scheduler;
	workqueue_t workqueue[VOXIA_MAX_WORKQUEUE_EACH_CORE];
	uint32_t workqueue_count;
	thread_t* active_thread;
} each_core_data;

void coreUpdateGs(uint8_t id);
uint8_t coreGetCpuID();
each_core_data* vxGetCoreData();
each_core_data* vxGetCoreDataByCoreID(uint8_t core_id);
uint8_t vxGetActiveCoreCount();

#endif // __HAL__CPU__CORE_H__