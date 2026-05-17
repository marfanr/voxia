#include "./scheduler.h"

#include "hal/acpi/hpet.h"
#include "hal/apic/apic.h"
#include "hal/cpu/core.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/register.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "libk/serial.h"
#include <str.h>
#include "memory/memory_utils.h"
#include "memory/slab.h"
#include "procc/thread.h"
#include "type.h"

static struct slab_cache* scheduler_cache = nullptr;
static scheduler_core_t scheduler[VOXIA_MAX_CORE] = {0};

boolean_t g__scheduler__is__running = 0;

extern boolean_t is_running_program;

scheduler_core_t* vxGetSchedulerCore(uint16_t core) {
	return &scheduler[core];
}

INIT(Scheduler) {
	vxCreateSlabCache(&scheduler_cache, "scheduler",
			  sizeof(scheduler_queue_t), 0, 0);
	LOG_INFO("Scheduler", "scheduler cache at 0x%x", scheduler_cache);
}

scheduler_queue_t* vxSchedulerGetCurrentQueue(uint16_t core) {
	return scheduler[core].current;
}

static void vxDeatachFromScheduler(scheduler_queue_t* current) {
	const uint16_t core_id = current->thread->core_affinity;
	spin_acquire(&scheduler[core_id].lock);

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

	spin_release(&scheduler[core_id].lock);
}

static void
vxSaveRegister(interrupt_stack_frame_t* stack, cpu_register_t* reg) {
	// fpu_state_t* fpu = (fpu_state_t*) ((uint8_t*) stack - 512);
	reg->rip = stack->rip;
	reg->cs = stack->cs;
	reg->rflags = stack->rflags;
	reg->rsp = stack->rsp;
	reg->ss = stack->ss;
	// memcopy(reg->fpu_state, stack->fpu_state, sizeof(reg->fpu_state));
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

static void
vxRestoreRegister(interrupt_stack_frame_t* stack, cpu_register_t* reg) {
	// fpu_state_t* fpu = (fpu_state_t*) ((uint8_t*) stack - 512);
	stack->rip = reg->rip;
	stack->cs = reg->cs;
	stack->rflags = reg->rflags;
	stack->rsp = reg->rsp;
	stack->ss = reg->ss;
	// memcopy(stack->fpu_state, reg->fpu_state, sizeof(stack->fpu_state));
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
	const uint16_t core_id = coreGetCpuID();

	spin_acquire(&scheduler[core_id].lock);

	if (!scheduler[core_id].run_queue_head) {
		spin_release(&scheduler[core_id].lock);
		return;
	}

	if (!scheduler[core_id].current)
		scheduler[core_id].current = scheduler[core_id].run_queue_head;

	scheduler_queue_t* current = scheduler[core_id].current;
	thread_t* thread = current->thread;

	uint64_t tick = vxHPETGetMainCount();

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
		thread->state = THREAD_STATE_RUNNING;

		if (thread->flags & THREAD_USER) {
			LOG2_DEBUG("SCHEDULER",
				   "core %d ready to run user mode", core_id);
		} else {
			LOG2_DEBUG("SCHEDULER",
				   "core %d ready to run kernel mode", core_id);

			reg->rip = thread->entry_addr;
			reg->rsp = ((thread->stack + 0x1000) & ~0xFULL) - 8;
			reg->cs = 0x28;
			reg->ss = 0x30;
			reg->rflags = 0x202;
			reg->rbp = 0;

			LOG2_DEBUG("SCHEDULER", "core %d aaddr %x", core_id,
				   thread->entry_addr);
			goto end_scheduler_tick;
		}
		goto end_scheduler_tick;
	}
	case THREAD_STATE_RUNNING: {
		vxSaveRegister(reg, &thread->reg);
		break;
	}
	case THREAD_STATE_TERMINATED: {

		thread->state = THREAD_STATE_HAL;
		LOG2_DEBUG("SCHEDULER", "core %d terminated, thread id %d",
			   core_id, thread->id);
		spin_release(&scheduler[core_id].lock);
		vxDeatachFromScheduler(current);
		LOG2_DEBUG("SCHEDULER", "TERMINATED");
		return;
	}

	default:
		break;
	}

	if (current->thread->flags & THREAD_PREEMPT_ENABLE) {
		/* context switching enable */
	}

	if (ns2ms(vxHPETGetMainCount() - thread->last_run_time)
	    > VOXIA_MAX_SCHEDULER_TIME_MS) {

		scheduler_queue_t* next = current->next_queue;

		if (next != current) {
			if (next->thread->state == THREAD_STATE_RUNNING) {
				vxRestoreRegister(reg, &next->thread->reg);
			}

			next->thread->last_run_time = vxHPETGetMainCount();
			next->thread->has_update_run_time = true;
		}

		thread->has_update_run_time = false;
		scheduler[core_id].current = next;
	}

end_scheduler_tick:
	spin_release(&scheduler[core_id].lock);
}

static scheduler_queue_t* vxAllocScheduler(const uint16_t core) {
	spin_acquire(&scheduler[core].lock);
	const auto queue = (scheduler_queue_t*) vxSlabAlloc(scheduler_cache);

	if (!queue) {
		spin_release(&scheduler[core].lock);
		return nullptr;
	}

	memset(queue, 0, sizeof(scheduler_queue_t));
	queue->prev_queue = nullptr;
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

	uint16_t starting_core = 1;
	if (new_thread->core_affinity != (uint16_t) -1)
		starting_core = new_thread->core_affinity;

	scheduler_queue_t* queue = vxAllocScheduler(starting_core);
	if (!queue) {
		LOG2_ERROR("SCHED", "error allocate new scheduler....");
		return;
	}

	LOG2_DEBUG("SCHED", "ATTACH thread id %d queue 0x%x", new_thread->id,
		   queue);
	new_thread->state = THREAD_STATE_READY;
	queue->thread = new_thread;
}

void vxStartScheduler() {
	const uint8_t core_id = coreGetCpuID();
	serial2_printf("scheduler init on core %d\n", core_id);
	irq_register(core_id, 0x45, (void*) vxSchedulerTick, true, 0x28, 0,
		     INTERRUPT_ATTR_KERNEL);
	vxAPICCreateTimer(APIC_TIMER_PERIOD, 8, 0x45);
}