#ifndef __PROCC__THREAD_H__
#define __PROCC__THREAD_H__

#include "autoconf.h"
#include "hal/cpu/register.h"
#include "procc/process.h"
#include <spinlock.h>

typedef uint64_t thread_id;

enum : uint16_t {
	THREAD_USER = (1 << 0),
	THREAD_PREEMPT_ENABLE = (1 << 1),
};

enum {
	THREAD_STATE_CREATE = 0,
	THREAD_STATE_READY = 1,
	THREAD_STATE_RUNNING = 2,
	THREAD_STATE_TERMINATED = 3,
	THREAD_STATE_HAL = 4,
};

typedef struct thread thread_t;
struct thread {
	thread_id id;
	uint16_t core_affinity;
	volatile uintptr_t* page;
	uint8_t state;
	uint8_t priority;
	uint16_t flags;
	uint64_t stack;
	uint64_t last_run_time;
	boolean_t has_update_run_time;
	uintptr_t entry_addr;
	uint16_t current_core_id;
	// 1 cache line

	process_t* process;
	uint32_t *clear_child_tid;
	uint32_t uuid;
	uint64_t fs_base;
	uint64_t gs_base;
	uint8_t _pad[12];
	// 1 cache line

	cpu_register_t reg;
} __attribute__((aligned(64)));

typedef struct {
	thread_t* thread;
	uint32_t gen;
	boolean_t used;
} thread_slot_t;

typedef struct thread_bucket {
	thread_slot_t slot[VOXIA_MAX_NUMBER_THREAD];
	spinlock_t lock;
	uint32_t top_free;
} thread_bucket_t;

#define THREAD_MAKE_ID(id, gen) ((uint64_t)(gen) << 32 | ((uint64_t)(id) + 1))
#define THREAD_GET_ID(tid) ((uint32_t)((tid) & 0xFFFFFFFFULL) - 1)
#define THREAD_GET_GEN(tid) ((uint32_t)((tid) >> 32))

thread_t* create_thread(volatile uintptr_t* page, uintptr_t entry, uintptr_t stack,
                        uint16_t core_affinity, uint8_t priority,
                        uint16_t flags);
void vxThreadExit(void);

#endif /* __PROCC__THREAD_H__ */