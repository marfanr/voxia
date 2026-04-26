#include "./scheduler.h"

#include "hal/acpi/hpet.h"
#include "hal/apic/apic.h"
#include "hal/cpu/core.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/register.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "memory/memory_utils.h"
#include "memory/slab.h"
#include "procc/thread.h"

static struct slab_cache* scheduler_cache = nullptr;
static scheduler_core_t scheduler[VOXIA_MAX_CORE] = {0};

boolean_t g__scheduler__is__running = 0;

extern boolean_t is_running_program;

scheduler_core_t* vxGetSchedulerCore(uint16_t core) { return &scheduler[core]; }

INIT(Scheduler) {
	vxCreateSlabCache(&scheduler_cache, "scheduler",
	                  sizeof(scheduler_queue_t), 0, 0);
}

scheduler_queue_t* vxSchedulerGetCurrentQueue(uint16_t core) {
	return scheduler[core].current;
}

static void vxDeatachFromScheduler(scheduler_queue_t* current) {
	const uint16_t core_id = current->thread->core_affinity;
	spin_acquire(&scheduler[current->thread->core_affinity].lock);

	if (current->next_queue == current)
		current->next_queue = 0;

	scheduler[core_id].current = current->next_queue;

	if (current->prev_queue)
		current->prev_queue->next_queue = current->next_queue;

	if (current == scheduler[core_id].last)
		scheduler[core_id].last = current->next_queue;

	scheduler[core_id].run_queue_head = current->next_queue;

	current->next_queue = nullptr;
	current->prev_queue = nullptr;
	spin_release(&scheduler[current->thread->core_affinity].lock);
}

static void vxSaveRegister(interrupt_stack_frame_t* stack,
                           cpu_register_t* reg) {
	reg->rip = stack->rip;
	reg->cs = stack->cs;
	reg->rflags = stack->rflags;
	reg->rsp = stack->rsp;
	reg->ss = stack->ss;
	memcopy(reg->fpu_state, stack->fpu_state, sizeof(reg->fpu_state));
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
	// memcopy((void *)reg, (void *)stack, sizeof(cpu_register_t));
}

static void vxRestoreRegister(interrupt_stack_frame_t* stack,
                              cpu_register_t* reg) {
	stack->rip = reg->rip;
	stack->cs = reg->cs;
	stack->rflags = reg->rflags;
	stack->rsp = reg->rsp;
	stack->ss = reg->ss;
	memcopy(stack->fpu_state, reg->fpu_state, sizeof(stack->fpu_state));
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
	// memcopy((void *)stack, (void *)reg, sizeof(cpu_register_t));
}

void vxSchedulerTick(interrupt_stack_frame_t* reg) {

	uint64_t tick = vxHPETGetMainCount();
	const uint16_t core_id = coreGetCpuID();
	if (!scheduler[core_id].run_queue_head)
		return;

	if (!scheduler[core_id].current)
		scheduler[core_id].current = scheduler[core_id].run_queue_head;

	scheduler_queue_t* current = scheduler[core_id].current;
	// LOG2_INFO("SCHEDULER", "on core id %d tick %d thread %d", core_id,
	// tick,
	//           current->thread->id);

	thread_t* thread = current->thread;

	if (!thread->has_update_run_time) {
		thread->last_run_time = tick;
		thread->has_update_run_time = true;
	}

	switch (thread->state) {
	case THREAD_STATE_CREATE: {
		LOG2_DEBUG("SCHEDULER", "thread create at core %d (%d)",
		           core_id, thread->id);
		break;
	}
	case THREAD_STATE_READY: {
		if (!thread->stack)
			thread->stack = (uintptr_t)kalloc(0x1000);

		// if (tick - thread->)
		thread->state = THREAD_STATE_RUNNING;

		// on debug
		if (thread->flags & THREAD_USER) {
			LOG2_DEBUG("SCHEDULER",
			           "core %d ready to run user mode", core_id);
		} else {
			LOG2_DEBUG("SCHEDULER",
			           "core %d ready to run kernel mode", core_id);
			LOG2_DEBUG("SCHEDULER", "core %d aaddr %x", core_id,
			           thread->entry_addr);
			((void (*)())thread->entry_addr)();
			reg->rip = thread->entry_addr;
			reg->rsp = ((thread->stack + 0x1000) & ~0xFULL) - 8;
			// LOG2_DEBUG("SCHEDULER", "core %d rsp
			// %x", core_id, 	           reg->rsp);
			reg->rbp = 0;
		}
		vxSaveRegister(reg, &thread->reg);
		break;
	}
	case THREAD_STATE_RUNNING: {
		// vxRestoreRegister(reg, &thread->reg);
		break;
	}
	case THREAD_STATE_TERMINATED: {
		current->thread->state = THREAD_STATE_HAL;
		LOG2_DEBUG("SCHEDULER", "core %d  terminated, thread id %d",
		           core_id, thread->id);
		vxDeatachFromScheduler(current);
		LOG2_DEBUG("SCHEDULER", "TERMINATED");
		return;
		break;
	}

	default:
	}

	if (current->thread->flags & THREAD_PREEMPT_ENABLE) {
		// context switching enable
	}

	// TODO: handle non preamable threadm

	if (ns2ms(vxHPETGetMainCount() - thread->last_run_time) >
	    VOXIA_MAX_SCHEDULER_TIME_MS) {
		// LOG2_DEBUG("SCHEDULER", "timout");
		if (current->next_queue != current) {
			vxSaveRegister(reg, &thread->reg);
			if (current->next_queue->thread->state ==
			    THREAD_STATE_RUNNING) {
				vxRestoreRegister(
				    reg, &current->next_queue->thread->reg);
				// LOG2_DEBUG("SCHEDULER", "restore thread %d",
				// current->next_queue->thread->id);
			}
		}

		thread->has_update_run_time = false;
		thread->last_run_time = vxHPETGetMainCount();
		scheduler[core_id].current = current->next_queue;
	}

	// if (core_id == 2)
	// 	serial2_flush();
}

static scheduler_queue_t* vxAllocScheduler(const uint16_t core) {
	spin_acquire(&scheduler[core].lock);
	const auto queue = (scheduler_queue_t*)vxSlabAlloc(scheduler_cache);
	queue->next_queue = scheduler[core].run_queue_head;

	if (!queue->next_queue) {
		queue->next_queue = queue;
	}

	if (!scheduler[core].last) {
		scheduler[core].last = queue;
	}

	scheduler[core].run_queue_head = queue;
	scheduler[core].last->next_queue = queue;
	spin_release(&scheduler[core].lock);
	return queue;
}

extern uintptr_t lapic_base_addr;

void vxAttachScheduler(thread_t* new_thread) {
	if (!new_thread)
		return;

	// TODO: cari core dengan beban terendah
	uint16_t starting_core = 1;
	if (new_thread->core_affinity != (uint16_t)-1)
		starting_core = new_thread->core_affinity;

	scheduler_queue_t* queue = vxAllocScheduler(starting_core);

	LOG2_DEBUG("SCHED", "ATTACH thread id %d queue 0x%x", new_thread->id,
	           queue);
	new_thread->state = THREAD_STATE_READY;
	queue->thread = new_thread;
}

void vxStartScheduler() {
	const uint16_t core_id = coreGetCpuID();
	serial2_printf("scheduler init on core %d\n", core_id);
	irq_register(core_id, 0x45, (void*)vxSchedulerTick, true, 0x28, 0,
	             INTERRUPT_ATTR_KERNEL);
	vxAPICCreateTimer(APIC_TIMER_PERIOD, 5, 0x45);
	// LOG2_DEBUG(DEBUG_LEVEL_INFO, "Scheduler started on CORE %d
	// \n",
	//            core_id);
}
