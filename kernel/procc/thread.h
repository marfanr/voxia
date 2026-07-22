#ifndef __PROCC__THREAD_H__
#define __PROCC__THREAD_H__

#include "autoconf.h"
#include "hal/cpu/register.h"
#include "procc/process.h"
#include "sys/sig.h"
#include <spinlock.h>

#define USER_STACK_PAGES 256
#define USER_STACK_SIZE (USER_STACK_PAGES * 4096)

typedef uint64_t thread_id;

enum : uint16_t {
	THREAD_USER = (1 << 0),
	THREAD_PREEMPT_ENABLE = (1 << 1),
};

enum {
	THREAD_STATE_CREATE = 0,
	THREAD_STATE_READY = 1,
	THREAD_STATE_RUNNING = 2,
	THREAD_STATE_BLOCKED = 3,
	THREAD_STATE_TERMINATED = 4,
	THREAD_STATE_HAL = 5,
};

typedef struct thread thread_t;
struct thread {
	thread_id id;
	uint16_t core_affinity;
	uint8_t state;
	uint8_t priority;
	uint16_t flags;
	uint64_t stack_top;
	uint64_t stack_base;
	uint64_t last_run_time;
	uint64_t total_run_time_ns;
	boolean_t has_update_run_time;
	uint16_t current_core_id;
	uintptr_t entry_addr;
	uint32_t* clear_child_tid;
	// 1 cache line

	process_t* process;
	struct thread* process_thread_next;
	struct thread* process_thread_prev;
	uint32_t uuid;
	uint64_t fs_base;
	uint64_t gs_base;	
	uintptr_t kernel_rsp;
	uintptr_t kernel_stack_base;
	uintptr_t kernel_stack_top;
	bool in_kernel_sleep;
	bool wake_pending;
	// 1 cache line
	sig_han_t* signal;

	cpu_register_t reg;
	uint64_t wakeup_time;
	cpu_register_t saved_reg;
	bool has_saved_reg;

	uint8_t* raw_fpu_ptr;
	uint8_t* fpu_state;
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

thread_t* create_thread(uintptr_t entry,
                        uintptr_t stack_top, uintptr_t stack_base,
                        uint16_t core_affinity, uint8_t priority,
                        uint16_t flags);
void thread_exit(void);
thread_t* vxGetThreadByIndex(uint32_t idx);
uint64_t vxGetThreadTotalRunTime(thread_t* thread);
uint16_t vxGetThreadCurrentCore(thread_t* thread);
thread_t* fork_process(thread_t* parent, interrupt_stack_frame_t* rsp);
void destroy_thread(thread_t* thr);
thread_id thrAcquireNewSlot();
void vxUpdateThreadSlot(const thread_id id, thread_t* thr);
thread_t* thrCreateInstance();

#endif /* __PROCC__THREAD_H__ */