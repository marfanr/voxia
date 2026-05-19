#ifndef __PROCC__THREAD_H__
#define __PROCC__THREAD_H__

#include "autoconf.h"
#include "hal/cpu/register.h"
#include "hal/cpu/spinlock.h"

typedef uint64_t thread_id;

/* Flags — bit positions, bukan bitmask komplemen */
enum : uint16_t {
	THREAD_USER           = (1 << 0),
	THREAD_PREEMPT_ENABLE = (1 << 1),
};
/*
 * "Kernel mode" = tidak ada THREAD_USER.
 * Untuk clear: flags &= ~THREAD_USER
 * Untuk disable preempt: flags &= ~THREAD_PREEMPT_ENABLE
 */

/* States — nilai unik, HAL != TERMINATED */
enum {
	THREAD_STATE_CREATE     = 0,
	THREAD_STATE_READY      = 1,
	THREAD_STATE_RUNNING    = 2,
	THREAD_STATE_TERMINATED = 3,
	THREAD_STATE_HAL        = 4, /* state akhir setelah di-detach dari queue */
};

typedef struct thread thread_t;
struct thread {
	/* --- Cache line 1 (scheduler hot path, 64B) --- */
	thread_id id;                  /* 8B */
	uint16_t  core_affinity;       /* 2B */
	uint8_t   state;               /* 1B */
	uint8_t   priority;            /* 1B */
	uint16_t  flags;               /* 2B */
	uint8_t   _pad0[2];            /* align last_run_time ke 8B boundary */
	uint64_t  last_run_time;       /* 8B */
	uint32_t  uuid;                /* 4B */
	boolean_t has_update_run_time; /* 1B */
	uint8_t   _pad1[3];            /* total hot fields = 32B */
	uint8_t   _cache_fill[32];     /* pad sisa cache line 1 ke 64B */

	/* --- Cache line 2+ (context, jarang diakses) --- */
	uintptr_t      entry_addr;
	uint64_t       stack;
	cpu_register_t reg;
} __attribute__((aligned(64)));
 
typedef struct {
	thread_t* thread;
	uint32_t  gen;
	boolean_t used;
} thread_slot_t;

typedef struct thread_bucket {
	thread_slot_t slot[VOXIA_MAX_NUMBER_THREAD];
	spinlock_t    lock;
	uint32_t      top_free;
} thread_bucket_t;

#define THREAD_MAKE_ID(id, gen) \
	((uint64_t)(gen) << 32 | ((uint64_t)(id) + 1))
#define THREAD_GET_ID(tid)  ((uint32_t)((tid) & 0xFFFFFFFFULL) - 1)
#define THREAD_GET_GEN(tid) ((uint32_t)((tid) >> 32))

thread_id vxCreateThread(uintptr_t entry, uint16_t core_affinity,
                         uint8_t priority, uint16_t flags);
void vxThreadExit(void);

#endif /* __PROCC__THREAD_H__ */