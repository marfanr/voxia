#ifndef __PROCC__SCHEDULER_H__
#define __PROCC__SCHEDULER_H__

#include <spinlock.h>
#include "procc/thread.h"
#include <type.h>

typedef struct scheduler_queue scheduler_queue_t;
struct scheduler_queue {
	thread_t*              thread;
	struct scheduler_queue* prev_queue;
	struct scheduler_queue* next_queue;
} __attribute__((aligned(64)));

typedef struct scheduler_core {
	scheduler_queue_t* current;
	scheduler_queue_t* last;
	scheduler_queue_t* run_queue_head;
	spinlock_t         lock;
} scheduler_core_t;

void               vxStartScheduler(void);
scheduler_core_t*  vxGetSchedulerCore(uint16_t core);
void               vxAttachScheduler(thread_t* new_thread);
scheduler_queue_t* vxSchedulerGetCurrentQueue(uint16_t core);

#endif /* __PROCC__SCHEDULER_H__ */