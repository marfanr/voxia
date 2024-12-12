#ifndef __PROCC__TASK_H__
#define __PROCC__TASK_H__

#include <libk/type.h>
#include <hal/cpu/register.h>

typedef enum {
	TASK_RUNNING,
	TASK_READY,
	TASK_WAITING,
	TASK_SUSPENDED,
	TASK_TERMINATED
} task_state_t;

typedef enum {
	TASK_PRIORITY_LOW,
	TASK_PRIORITY_MEDIUM,
	TASK_PRIORITY_HIGH
} task_priority_t;

typedef struct {
	uint64_t pid;
	cpu_register_t regs;
	task_state_t state;
	task_priority_t priority;
} task_t;

#endif // __PROCC__TASK_H__