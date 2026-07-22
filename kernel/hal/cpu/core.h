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

typedef struct __attribute__((aligned(64))) {
	uint64_t canary;
	uint8_t core_id;
	
	uint64_t kernel_rsp;
	uint64_t user_rsp;
	int next_is_user;	
	
	boolean_t usleep_trigerred;
	scheduler_core_t* scheduler;
	uint32_t workqueue_count;
	uint32_t last_allocated_slot;
	thread_t* active_thread;
	

	boolean_t simd_has_avx;
	boolean_t simd_has_avx2;
	// cache line end

	workqueue_t* wq_head;
	workqueue_t* wq_tail;
	spinlock_t wq_lock;
} each_core_data;

void update_core_gs(uint8_t id);
uint8_t get_current_core_cpuid();
each_core_data* get_current_core_data();
each_core_data* vxGetCoreDataByCoreID(uint8_t core_id);
uint8_t vxGetActiveCoreCount();
uint32_t vxGetApicID(void);

#ifdef __cplusplus
}
#endif

#endif // __HAL__CPU__CORE_H__