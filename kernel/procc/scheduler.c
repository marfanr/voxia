#include "./scheduler.h"

#include "hal/acpi/hpet.h"
#include "hal/apic/apic.h"
#include "hal/cpu/gdt.h"
#include "hal/cpu/irq_lock.h"
#include "hal/cpu/msr.h"
#include "hal/cpu/paging.h"
#include "hal/cpu/register.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/slab.h"
#include "procc/thread.h"
#include "type.h"
#include <hal/cpu/core.h>
#include <str.h>

extern void kernel_context_save(uintptr_t* save_rsp, uintptr_t sched_rsp,
                                uintptr_t entry_point);
extern void kernel_context_restore(uintptr_t restore_rsp);

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
	uint16_t core_id = current->thread->core_affinity;

	if (core_id >= VOXIA_MAX_CORE) {
		core_id = get_current_core_cpuid();
	}

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

static void vxSaveRegister(volatile interrupt_stack_frame_t* stack,
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

static void vxRestoreRegister(volatile interrupt_stack_frame_t* stack,
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

static void vxSchedulerTick(volatile interrupt_stack_frame_t* reg) {
	auto current_core = get_current_core_data();
	if (!current_core)
		goto done_eoi;

	const uint16_t core_id = current_core->core_id;

	spin_acquire(&scheduler[core_id].lock);

	if (!scheduler[core_id].run_queue_head) {
		spin_release(&scheduler[core_id].lock);
		goto done_eoi;
	}

	if (!scheduler[core_id].current)
		scheduler[core_id].current = scheduler[core_id].run_queue_head;

	scheduler_queue_t* current_node = scheduler[core_id].current;
	if (!current_node || !current_node->thread) {
		spin_release(&scheduler[core_id].lock);
		goto done_eoi;
	}

	thread_t* thread = current_node->thread;
	uint64_t current_tick = vxHPETGetMainCount();
	bool needs_context_switch = false;

	if (!thread->has_update_run_time) {
		thread->last_run_time = current_tick;
		thread->has_update_run_time = true;
	}

	if (thread->state == THREAD_STATE_RUNNING) {
		vxSaveRegister(reg, &thread->reg);
		thread->fs_base = msrReadFSBase();
		thread->gs_base = msrReadKernelGSBase();

		uint64_t elapsed_ns =
		    (current_tick - thread->last_run_time) * vxHPETMinTickNs();
		if (ns2ms(elapsed_ns) > VOXIA_MAX_SCHEDULER_TIME_MS) {
			needs_context_switch = true;
			thread->has_update_run_time = false;
		}
	} else if (thread->state == THREAD_STATE_BLOCKED ||
	           thread->state == THREAD_STATE_TERMINATED) {
		if (thread->state == THREAD_STATE_TERMINATED) {
			LOG2_DEBUG("SCHEDULER",
			           "core %d terminated thread id %d", core_id,
			           thread->id);
			thread->state = THREAD_STATE_HAL;
			vxDeatachFromScheduler(current_node, true);
		}
		needs_context_switch = true;
		if (!scheduler[core_id].run_queue_head) {
			spin_release(&scheduler[core_id].lock);
			goto done_eoi;
		}
	}

	scheduler_queue_t* next_node = current_node;

	if (needs_context_switch) {
		scheduler_queue_t* start = current_node->next_queue;
		if (!start)
			start = scheduler[core_id].run_queue_head;

		scheduler_queue_t* search = start;
		scheduler_queue_t* found = NULL;

		if (search) {
			do {
				thread_t* t = search->thread;
				if (t && (t->state == THREAD_STATE_RUNNING ||
				          t->state == THREAD_STATE_READY)) {
					found = search;
					break;
				}
				search = search->next_queue;
			} while (search != start);
		}

		if (found)
			next_node = found;
		else
			next_node = start;

		scheduler[core_id].current = next_node;
	}

	thread_t* next_thread = next_node->thread;

	if (next_thread->in_kernel_sleep) {
		volatile uintptr_t* reload_page = next_thread->page;
		set_tss_stack(core_id, next_thread->kernel_stack_top);

		next_thread->current_core_id = core_id;
		current_core->active_thread = next_thread;
		current_core->next_is_user =
		    (next_thread->flags & THREAD_USER) ? 1 : 0;

		spin_release(&scheduler[core_id].lock);

		if (reload_page)
			paging_reload(reload_page);

		/* doing long jump via kernel stack */
		apic_eoi();
		kernel_context_restore(next_thread->kernel_rsp);
		__builtin_unreachable();
	}

	/* new thread */
	if (next_thread->state == THREAD_STATE_READY) {
		next_thread->state = THREAD_STATE_RUNNING;
		set_tss_stack(core_id, next_thread->kernel_stack_top);

		if (next_thread->flags & THREAD_USER) {
			reg->rip = next_thread->entry_addr;
			reg->rsp = next_thread->stack;
			reg->cs = 0x48 | 3;
			reg->ss = 0x40 | 3;
			reg->rflags = 0x202;
			reg->rbp = 0;
		} else {
			reg->rip = next_thread->entry_addr;
			reg->rsp =
			    ((next_thread->stack + 0x1000) & ~(uint64_t)0xF) -
			    8;
			reg->cs = 0x28;
			reg->ss = 0x30;
			reg->rflags = 0x202;
			reg->rbp = 0;
		}

		next_thread->current_core_id = core_id;
		current_core->active_thread = next_thread;
		current_core->next_is_user =
		    (next_thread->flags & THREAD_USER) ? 1 : 0;

	/* running thread*/
	} else if (next_thread->state == THREAD_STATE_RUNNING) {
		if (needs_context_switch && next_thread != thread) {
			vxRestoreRegister(reg, &next_thread->reg);
			msrSetFSBase(next_thread->fs_base);
			msrSetKernelGSBase(next_thread->gs_base);
			set_tss_stack(core_id, next_thread->kernel_stack_top);

			next_thread->current_core_id = core_id;
			current_core->active_thread = next_thread;
			current_core->next_is_user =
			    (next_thread->flags & THREAD_USER) ? 1 : 0;
		}
	}

	if (needs_context_switch || !next_thread->has_update_run_time) {
		next_thread->last_run_time = vxHPETGetMainCount();
		next_thread->has_update_run_time = true;
	}

	volatile uintptr_t* reload_page = next_thread->page;
	spin_release(&scheduler[core_id].lock);

	if (reload_page)
		paging_reload(reload_page);

done_eoi:
	serial2_flush();
}

void thread_block() {
	thread_t* thread = get_current_core_data()->active_thread;
	uint16_t core_id = thread->current_core_id;

	uintptr_t flags = irq_save();
	thread->in_kernel_sleep = true;

	if (thread->wake_pending) {
		thread->wake_pending = false;
		thread->in_kernel_sleep = false;
		irq_restore(flags);
		return;
	}

	thread->state = THREAD_STATE_BLOCKED;

	extern void scheduler_resume_point(void);
	kernel_context_save(&thread->kernel_rsp, scheduler[core_id].sched_rsp,
	                    (uintptr_t)scheduler_resume_point);

	thread->in_kernel_sleep = false;
	thread->wake_pending = false;
	thread->state = THREAD_STATE_RUNNING;
	irq_restore(flags);
}

void schedule_yield() {
	thread_t* thread = get_current_core_data()->active_thread;
	uint16_t core_id = thread->current_core_id;

	uintptr_t flags = irq_save();
	thread->in_kernel_sleep = true;
	thread->state = THREAD_STATE_READY;

	kernel_context_save(&thread->kernel_rsp, scheduler[core_id].sched_rsp,
	                    (uintptr_t)scheduler_resume_point);

	thread->in_kernel_sleep = false;
	thread->state = THREAD_STATE_RUNNING;
	irq_restore(flags);
}

void scheduler_resume_point(void) {
	uint16_t core_id = get_current_core_cpuid();

	while (1) {
		__asm__ volatile("sti\nnop\ncli" ::: "memory");
		spin_acquire(&scheduler[core_id].lock);

		scheduler_queue_t* start = scheduler[core_id].current;
		if (!start)
			start = scheduler[core_id].run_queue_head;

		scheduler_queue_t* node = start;
		scheduler_queue_t* found = NULL;

		if (node) {
			do {
				thread_t* t = node->thread;
				if (t && (t->state == THREAD_STATE_RUNNING ||
				          t->state == THREAD_STATE_READY)) {
					found = node;
					break;
				}
				node = node->next_queue;
			} while (node && node != start);
		}

		if (found) {
			scheduler[core_id].current = found;
		}
		spin_release(&scheduler[core_id].lock);

		if (!found) {
			__asm__ volatile("sti\npause");
			continue;
		}

		thread_t* next = found->thread;

		if (next->in_kernel_sleep) {
			volatile uintptr_t* reload_page = next->page;
			set_tss_stack(core_id, next->kernel_stack_top);

			auto core_data_ = get_current_core_data();
			next->current_core_id = core_id;
			core_data_->active_thread = next;
			core_data_->next_is_user =
			    (next->flags & THREAD_USER) ? 1 : 0;

			msrSetFSBase(next->fs_base);
			msrSetKernelGSBase(next->gs_base);

			if (reload_page)
				paging_reload(reload_page);

			kernel_context_restore(next->kernel_rsp);
			__builtin_unreachable();

		} else {
			/*
			 * Non-kernel-sleep thread
			 * Just re-enable interrupts and spin, the timer ISR
			 * takes over.
			 */
			__asm__ volatile("sti\nhlt\ncli" ::: "memory");
		}
	}
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

extern uint8_t vxGetActiveCoreCount();

void attach_to_scheduler(thread_t* new_thread) {
	if (!new_thread)
		return;

	// TODO: lock

	uint16_t core = 0;
	if (new_thread->core_affinity != (uint16_t)-1) {
		core = new_thread->core_affinity;
	} else {
		uint16_t jum_core = vxGetActiveCoreCount();
		if (jum_core > 1) {
			static uint16_t next_core_hint = 1;
			core = (__atomic_fetch_add(&next_core_hint, 1,
			                           __ATOMIC_RELAXED) %
			        (jum_core - 1)) +
			       1;
		} else {
			core = 0;
		}
	}

	new_thread->core_affinity = core;
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

void vxStartScheduler(void) {
	const uint8_t core_id = get_current_core_cpuid();

	void* stack = kalloc(SCHEDULER_STACK_SIZE);
	if (!stack) {
		serial_printf(
		    "PANIC: failed to alloc sched stack for core %d\n",
		    core_id);
		return;
	}
	memset(stack, 0, SCHEDULER_STACK_SIZE);

	scheduler[core_id].sched_stack = (uintptr_t)stack;
	uintptr_t sched_top =
	    scheduler[core_id].sched_stack + SCHEDULER_STACK_SIZE;

	/* put scheduelr_resume_point into stack */
	scheduler[core_id].sched_rsp = (sched_top & ~(uintptr_t)0xF) - 16;
	*(uintptr_t*)(scheduler[core_id].sched_rsp) =
	    (uintptr_t)scheduler_resume_point;

	serial_printf("scheduler init on core %d 0x%x\n", core_id,
	              vxSchedulerTick);

	irq_register(core_id, 0x45, (void*)vxSchedulerTick, true, 0x28, 0,
	             INTERRUPT_ATTR_KERNEL);

	vxAPICCreateTimer(APIC_TIMER_PERIOD, 100, 0x45);
}

void vxThreadWake(thread_t* thread) {
	if (!thread)
		return;

	thread->wake_pending = true;
	if (thread->state == THREAD_STATE_BLOCKED) {
		thread->state = THREAD_STATE_RUNNING;
	}
}

void sch_restore_to_next_thread(volatile interrupt_stack_frame_t* rsp,
                                uint16_t core_id) {
	spin_acquire(&scheduler[core_id].lock);

	scheduler_queue_t* current_node = scheduler[core_id].current;
	if (!current_node || !current_node->thread) {
		spin_release(&scheduler[core_id].lock);
		return;
	}

	thread_t* crashed = current_node->thread;
	crashed->state = THREAD_STATE_HAL;

	// Detach dari queue (already_locked = true)
	vxDeatachFromScheduler(current_node, true);

	// Tidak ada thread lain → tidak bisa recover
	if (!scheduler[core_id].run_queue_head) {
		spin_release(&scheduler[core_id].lock);
		INFLOOP;
	}

	scheduler_queue_t* next_node = scheduler[core_id].current;
	if (!next_node)
		next_node = scheduler[core_id].run_queue_head;

	thread_t* next = next_node->thread;

	// Restore FULL register context
	if (next->state == THREAD_STATE_RUNNING) {
		vxRestoreRegister(rsp, &next->reg);
		msrSetFSBase(next->fs_base);
		msrSetKernelGSBase(next->gs_base);
	} else if (next->state == THREAD_STATE_READY) {
		// setup new thread
		next->state = THREAD_STATE_RUNNING;
		if (next->flags & THREAD_USER) {
			rsp->rip = next->entry_addr;
			rsp->rsp = next->stack;
			rsp->cs = 0x48 | 3;
			rsp->ss = 0x40 | 3;
			rsp->rflags = 0x202;
			rsp->rbp = 0;
		} else {
			rsp->rip = next->entry_addr;
			rsp->rsp =
			    ((next->stack + 0x1000) & ~(uint64_t)0xF) - 8;
			rsp->cs = 0x28;
			rsp->ss = 0x30;
			rsp->rflags = 0x202;
			rsp->rbp = 0;
		}
	}

	auto current_core = get_current_core_data();
	next->current_core_id = core_id;
	current_core->active_thread = next;
	current_core->next_is_user = (next->flags & THREAD_USER) ? 1 : 0;

	scheduler[core_id].current = next_node;

	volatile uintptr_t* reload_page = next->page;
	spin_release(&scheduler[core_id].lock);

	if (reload_page)
		paging_reload(reload_page);
}