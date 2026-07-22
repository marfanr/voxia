#ifndef __PROCC__THREAD_H__
#define __PROCC__THREAD_H__

#include "autoconf.h"
#include <register.h>
#include <spinlock.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t thread_id;

enum {
	THREAD_USER = (1 << 0),
	THREAD_KERNEL = ~(1 << 0),
	THREAD_PREEMPT_ENABLE = (1 << 1),
	THREAD_PREEMPT_DISABLE = ~(1 << 1)
};

enum {
	THREAD_STATE_CREATE = 0,
	THREAD_STATE_READY = 1,
	THREAD_STATE_RUNNING = 2,
	THREAD_STATE_HAL = 3,
	THREAD_STATE_TERMINATED = 3,
};

typedef struct thread thread_t;
struct thread {
	// --- Cache line 1 (scheduler hot path) ---
	thread_id id;		// 4B
	uint16_t core_affinity; // 2B
	uint8_t state;		// 1B
	uint8_t priority;	// 1B
	uint16_t flags;		// 2B
	uint64_t last_run_time; // 8B profiling
	uint64_t total_run_time_ns; // 8B CPU Usage profiling
	uint32_t uuid;
	boolean_t has_update_run_time;
	uint8_t _pad1[64 - 37]; // align ke 64B

	// --- Cache line 2–4 (context, jarang diakses) ---
	uintptr_t entry_addr; // 8B
	uint64_t stack;	      // 8B
	cpu_register_t reg;   // 128–160B
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

#define THREAD_MAKE_ID(id, gen) ((uint64_t) (gen) << 32 | ((uint64_t) (id) + 1))
#define THREAD_GET_ID(thread_id) ((uint32_t) ((thread_id) & 0xFFFFFFFFULL) - 1)
#define THREAD_GET_GEN(thread_id) ((uint32_t) ((thread_id) >> 32))

thread_id vxCreateThread(const uintptr_t entry, uint16_t core_affinity,
			 uint8_t priority, uint16_t flags);
void vxThreadExit();
thread_t* vxGetThreadByIndex(uint32_t idx);
uint64_t vxGetThreadTotalRunTime(thread_t* thread);
uint16_t vxGetThreadCurrentCore(thread_t* thread);
void thread_sleep(uint64_t ms);

#ifdef __cplusplus
}
#endif

#endif // __PROCC__THREAD_H__