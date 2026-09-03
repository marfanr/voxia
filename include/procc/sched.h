#ifndef __PROCC__SCHED_H__
#define __PROCC__SCHED_H__

#include <type.h>
#include <procc/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

void thread_block();
boolean_t vxIsSchedulerRunning();
void vxThreadWake(thread_t* thread);
void schedule_yield();

#ifdef __cplusplus
}
#endif

#endif // __PROCC__SCHED_H__