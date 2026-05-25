#include "procc/thread.h"
#include "autoconf.h"
#include "init/init.h"
#include "libk/atomic.h"
#include "libk/serial.h"
#include "memory/slab.h"
#include "scheduler.h"
#include "type.h"
#include <hal/cpu/core.h>

static struct slab_cache* thread_cache = nullptr;
static thread_bucket_t bucket = {0};

INIT(Thread) {
	vxCreateSlabCache(&thread_cache, "thread", sizeof(thread_t), 0, 0);
}

static thread_id thrAcquireNewSlot() {
	for (thread_id i = bucket.top_free; i < VOXIA_MAX_NUMBER_THREAD; i++) {
		if (!bucket.slot[i].used) {
			bucket.slot[i].used = true;
			bucket.slot[i].gen++;
			LOG2_DEBUG("THREAD", "NEW SLOT Gen %d Id %d",
			           bucket.slot[i].gen, i);
			return THREAD_MAKE_ID(i, bucket.slot[i].gen);
		}
	}
	bucket.top_free++;
	return nullptr;
}

static thread_t* thrCreateInstance() {
	return (thread_t*)vxSlabAlloc(thread_cache);
}

// TODO: unused
// static thread_t* thrGetById(const thread_id id) {
// 	const uint32_t idx = THREAD_GET_ID(id);
// 	return bucket.slot[idx].thread;
// }

static void vxUpdateThreadSlot(const thread_id id, thread_t* thr) {
	const uint32_t idx = THREAD_GET_ID(id);
	bucket.slot[idx].thread = thr;
}

thread_t* create_thread(volatile uintptr_t* page, uintptr_t entry,
                        uintptr_t stack, uint16_t core_affinity,
                        uint8_t priority, uint16_t flags) {
	thread_t* thr = thrCreateInstance();
	serial2_printf("created thread at 0x%x \n", thr);
	thr->id = thrAcquireNewSlot();
	thr->entry_addr = entry;
	thr->core_affinity = core_affinity;
	thr->priority = priority;
	thr->flags = flags;
	thr->stack = stack;
	thr->state = THREAD_STATE_CREATE;
	thr->page = page;

	vxUpdateThreadSlot(thr->id, thr);
	LOG2_DEBUG("THREAD", "created thread %d", thr->id);
	return thr;
}

void vxThreadExit() {
	const uint16_t core_id = get_current_core_cpuid();
	auto queue = vxSchedulerGetCurrentQueue(core_id);
	queue->thread->state = THREAD_STATE_TERMINATED;
	for (;;)
		__asm__ volatile(
		    "hlt"); // LOG_DEBUG("THREAD", "acalled thread exit");
}