#ifndef __PROCC__SCHEDULER_H__
#define __PROCC__SCHEDULER_H__

#include "hal/cpu/interrupt.h"
#include "procc/thread.h"
#include <spinlock.h>
#include <type.h>

typedef struct scheduler_queue scheduler_queue_t;
struct scheduler_queue {
	thread_t* thread;
	struct scheduler_queue* prev_queue;
	struct scheduler_queue* next_queue;
} __attribute__((aligned(64)));

#define SCHEDULER_STACK_SIZE 0x4000
typedef struct scheduler_core {
	scheduler_queue_t* current;
	scheduler_queue_t* last;
	scheduler_queue_t* run_queue_head;
	spinlock_t lock;
	uintptr_t sched_stack;
	uintptr_t sched_rsp;
	boolean_t is_running;
	uint16_t timer_vector;
	uint64_t tick_interval_us;
} scheduler_core_t;

void vxStartScheduler(void);
boolean_t vxIsSchedulerRunning();
scheduler_core_t* vxGetSchedulerCore(uint16_t core);
void attach_to_scheduler(thread_t* new_thread);
scheduler_queue_t* vxSchedulerGetCurrentQueue(uint16_t core);
void sch_restore_to_next_thread(volatile interrupt_stack_frame_t* rsp,
                                uint16_t core_id);
void schedule_yield();
void thread_block();
void scheduler_resume_point(void);
void vxThreadWake(thread_t* thread);
void vxSaveRegister(volatile interrupt_stack_frame_t* stack, cpu_register_t* reg);
void vxRestoreRegister(volatile interrupt_stack_frame_t* stack, cpu_register_t* reg);
void thread_sleep(uint64_t ms);
#endif /* __PROCC__SCHEDULER_H__ */