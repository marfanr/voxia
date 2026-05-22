#include "./scheduler.h"

#include "hal/acpi/hpet.h"
#include "hal/apic/apic.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "hal/cpu/register.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/slab.h"
#include "procc/thread.h"
#include "type.h"
#include <hal/cpu/core.h>
#include <str.h>

static struct slab_cache* scheduler_cache = nullptr;
static scheduler_core_t scheduler[VOXIA_MAX_CORE] = {0};
extern each_core_data core_data[VOXIA_MAX_CORE];
boolean_t g__scheduler__is__running = 0;

scheduler_core_t* vxGetSchedulerCore(uint16_t core) { return &scheduler[core]; }

INIT(Scheduler) {
	vxCreateSlabCache(&scheduler_cache, "scheduler",
	                  sizeof(scheduler_queue_t), 0, 0);
	LOG_INFO("Scheduler", "scheduler cache at 0x%x", scheduler_cache);
}

scheduler_queue_t* vxSchedulerGetCurrentQueue(uint16_t core) {
	return scheduler[core].current;
}

static void vxDeatachFromScheduler(scheduler_queue_t* current,
                                   bool already_locked) {
	const uint16_t core_id = current->thread->core_affinity;

	if (!already_locked)
		spin_acquire(&scheduler[core_id].lock);

	if (current->next_queue == current || current->next_queue == nullptr) {
		scheduler[core_id].run_queue_head = nullptr;
		scheduler[core_id].last = nullptr;
		scheduler[core_id].current = nullptr;
	} else {
		if (current->prev_queue)
			current->prev_queue->next_queue = current->next_queue;
		if (current->next_queue)
			current->next_queue->prev_queue = current->prev_queue;

		if (scheduler[core_id].run_queue_head == current)
			scheduler[core_id].run_queue_head = current->next_queue;
		if (scheduler[core_id].last == current)
			scheduler[core_id].last = current->prev_queue;
		if (scheduler[core_id].current == current)
			scheduler[core_id].current = current->next_queue;
	}

	current->next_queue = nullptr;
	current->prev_queue = nullptr;

	if (!already_locked)
		spin_release(&scheduler[core_id].lock);
}

static void vxSaveRegister(interrupt_stack_frame_t* stack,
                           cpu_register_t* reg) {
	reg->rip = stack->rip;
	reg->cs = stack->cs;
	reg->rflags = stack->rflags;
	reg->rsp = stack->rsp;
	reg->ss = stack->ss;
	reg->rax = stack->rax;
	reg->rbx = stack->rbx;
	reg->rcx = stack->rcx;
	reg->rdx = stack->rdx;
	reg->rbp = stack->rbp;
	reg->rsi = stack->rsi;
	reg->rdi = stack->rdi;
	reg->r8 = stack->r8;
	reg->r9 = stack->r9;
	reg->r10 = stack->r10;
	reg->r11 = stack->r11;
	reg->r12 = stack->r12;
	reg->r13 = stack->r13;
	reg->r14 = stack->r14;
	reg->r15 = stack->r15;
}

static void vxRestoreRegister(interrupt_stack_frame_t* stack,
                              cpu_register_t* reg) {
	stack->rip = reg->rip;
	stack->cs = reg->cs;
	stack->rflags = reg->rflags;
	stack->rsp = reg->rsp;
	stack->ss = reg->ss;
	stack->rax = reg->rax;
	stack->rbx = reg->rbx;
	stack->rcx = reg->rcx;
	stack->rdx = reg->rdx;
	stack->rbp = reg->rbp;
	stack->rsi = reg->rsi;
	stack->rdi = reg->rdi;
	stack->r8 = reg->r8;
	stack->r9 = reg->r9;
	stack->r10 = reg->r10;
	stack->r11 = reg->r11;
	stack->r12 = reg->r12;
	stack->r13 = reg->r13;
	stack->r14 = reg->r14;
	stack->r15 = reg->r15;
}

static void vxSchedulerTick(interrupt_stack_frame_t* reg) {
	auto current_core = get_current_core_data();
	const uint16_t core_id = current_core->core_id;

	spin_acquire(&scheduler[core_id].lock);

	if (!scheduler[core_id].run_queue_head) {
		spin_release(&scheduler[core_id].lock);
		return;
	}

	if (!scheduler[core_id].current)
		scheduler[core_id].current = scheduler[core_id].run_queue_head;

	scheduler_queue_t* current = scheduler[core_id].current;
	if (!current) {
		spin_release(&scheduler[core_id].lock);
		return;
	}

	thread_t* thread = current->thread;
	if (!thread) {
		spin_release(&scheduler[core_id].lock);
		return;
	}

	uint64_t tick = vxHPETGetMainCount();

	if (!thread->has_update_run_time) {
		thread->last_run_time = tick;
		thread->has_update_run_time = true;
	}

	switch (thread->state) {
	case THREAD_STATE_CREATE:
		LOG2_DEBUG("SCHEDULER", "thread create at core %d (id %d)",
		           core_id, thread->id);
		break;

	case THREAD_STATE_READY: {
		thread->state = THREAD_STATE_RUNNING;

		// TODO: handle error interrupt on userspace
		if (thread->flags & THREAD_USER) {
			reg->rip = thread->entry_addr;
			reg->rsp =
			    ((thread->stack + 0x1000) & ~(uint64_t)0xF) - 8;
			reg->cs = 0x48 | 3; /* user code segment  (ring 3) */
			reg->ss = 0x40 | 3; /* user stack segment (ring 3) */
			reg->rflags = 0x202;
			reg->rbp = 0;

			LOG2_DEBUG("SCHEDULER",
			           "core %d ready: user mode rip=0x%x", core_id,
			           thread->entry_addr);
		} else {
			reg->rip = thread->entry_addr;
			reg->rsp =
			    ((thread->stack + 0x1000) & ~(uint64_t)0xF) - 8;
			reg->cs = 0x28; /* kernel code segment */
			reg->ss = 0x30; /* kernel stack segment */
			reg->rflags = 0x202;
			reg->rbp = 0;
			LOG2_DEBUG("SCHEDULER",
			           "core %d ready: kernel mode rip=0x%x",
			           core_id, thread->entry_addr);

			thread->current_core_id = core_id;
		}
		current_core->active_thread = thread;

		goto end_scheduler_tick;
	}

	case THREAD_STATE_RUNNING:
		vxSaveRegister(reg, &thread->reg);
		thread->current_core_id = core_id;
		current_core->active_thread = thread;
		break;

	case THREAD_STATE_TERMINATED:
		LOG2_DEBUG("SCHEDULER", "core %d terminated thread id %d",
		           core_id, thread->id);
		thread->state = THREAD_STATE_HAL;
		vxDeatachFromScheduler(current, true);
		spin_release(&scheduler[core_id].lock);
		return;

	default:
		break;
	}

	if (ns2ms(vxHPETGetMainCount() - thread->last_run_time) >
	    VOXIA_MAX_SCHEDULER_TIME_MS) {

		scheduler_queue_t* next = current->next_queue;

		if (!next || next == current)
			goto end_scheduler_tick;

		if (next->thread->state == THREAD_STATE_RUNNING)
			vxRestoreRegister(reg, &next->thread->reg);
		
		current_core->active_thread = next->thread;

		next->thread->last_run_time = vxHPETGetMainCount();
		next->thread->has_update_run_time = true;
		thread->has_update_run_time = false;
		scheduler[core_id].current = next;
	}

end_scheduler_tick:
	spin_release(&scheduler[core_id].lock);
}

static scheduler_queue_t* vxAllocScheduler(const uint16_t core) {
	spin_acquire(&scheduler[core].lock);

	scheduler_queue_t* queue =
	    (scheduler_queue_t*)vxSlabAlloc(scheduler_cache);

	if (!queue) {
		spin_release(&scheduler[core].lock);
		return nullptr;
	}

	memset(queue, 0, sizeof(scheduler_queue_t));

	if (!scheduler[core].run_queue_head) {
		queue->next_queue = queue;
		queue->prev_queue = queue;
		scheduler[core].run_queue_head = queue;
		scheduler[core].last = queue;
	} else {
		scheduler_queue_t* head = scheduler[core].run_queue_head;
		scheduler_queue_t* last = scheduler[core].last;

		last->next_queue = queue;
		queue->prev_queue = last;
		queue->next_queue = head;
		head->prev_queue = queue;
		scheduler[core].last = queue;
	}

	spin_release(&scheduler[core].lock);
	return queue;
}

void vxAttachScheduler(thread_t* new_thread) {
	if (!new_thread)
		return;

	uint16_t core = 1;
	if (new_thread->core_affinity != (uint16_t)-1)
		core = new_thread->core_affinity;

	scheduler_queue_t* queue = vxAllocScheduler(core);
	if (!queue) {
		LOG2_ERROR("SCHED", "error allocate scheduler for thread %d",
		           new_thread->id);
		return;
	}

	LOG2_DEBUG("SCHED", "ATTACH thread id %d -> queue 0x%x", new_thread->id,
	           queue);
	new_thread->state = THREAD_STATE_READY;
	queue->thread = new_thread;
}

#define APIC_TIMER_MASKED (1 << 16)

void vxStartScheduler(void) {
	const uint8_t core_id = get_current_core_cpuid();

	serial_printf("scheduler init on core %d 0x%x\n", core_id,
	              vxSchedulerTick);

	irq_register(core_id, 0x45, (void*)vxSchedulerTick, true, 0x28, 0,
	             INTERRUPT_ATTR_KERNEL);

	vxAPICCreateTimer(APIC_TIMER_PERIOD, 100, 0x45);
}