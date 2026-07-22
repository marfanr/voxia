#include "procc/scheduler.h"
#include "hal/acpi/acpi.h"
#include "hal/acpi/hpet.h"
#include "hal/apic/apic.h"
#include "hal/cpu/gdt.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "hal/cpu/paging.h"
#include "hal/cpu/register.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/slab.h"
#include "procc/thread.h"
#include "type.h"
#include <cpu/irq_lock.h>
#include <hal/cpu/core.h>
#include <procc/sched.h>
#include <str.h>

extern void kernel_context_save(uintptr_t* save_rsp, uintptr_t sched_rsp,
                                uintptr_t entry_point);
extern void kernel_context_restore(uintptr_t restore_rsp);

static struct slab_cache* scheduler_cache = nullptr;
static scheduler_core_t scheduler[VOXIA_MAX_CORE] = {0};
extern each_core_data core_data[VOXIA_MAX_CORE];
static void scheduler_resume_switch(thread_t* next_thread, each_core_data* current_core);

scheduler_core_t* vxGetSchedulerCore(uint16_t core) { return &scheduler[core]; }

INIT(Scheduler) {
	vxCreateSlabCache(&scheduler_cache, "scheduler",
	                  sizeof(scheduler_queue_t), 0, 0);
	LOG2_INFO("Scheduler", "scheduler cache at 0x%x", scheduler_cache);
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

	uintptr_t flags = 0;
	if (!already_locked) {
		flags = irq_save();
		spin_acquire(&scheduler[core_id].lock);
	}

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

	if (!already_locked) {
		spin_release(&scheduler[core_id].lock);
		irq_restore(flags);
	}
}

static void sch_idle_loop(void) {
	while(1) {
		__asm__ volatile("sti\n\thlt");
	}
}


void vxSaveRegister(volatile interrupt_stack_frame_t* stack,
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

void vxRestoreRegister(volatile interrupt_stack_frame_t* stack,
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

extern void tty_check_and_flush(void);

static void vxSchedulerTick(volatile interrupt_stack_frame_t* reg) {
	static uint64_t heartbeat[VOXIA_MAX_CORE] = {0};
	auto current_core = get_current_core_data();
	if (!current_core) {
		serial2_printf("current core empty\n");
		goto done_eoi;
	}
	const uint16_t core_id = current_core->core_id;
	heartbeat[core_id]++;

	// Re-arm timer for the next tick (TSC-Deadline/One-Shot)
	if (scheduler[core_id].tick_interval_us > 0) {
		vxAPICCreateTimer(APIC_TIMER_ONE_SHOT,
		                  scheduler[core_id].tick_interval_us,
		                  scheduler[core_id].timer_vector);
	}

	spin_acquire(&scheduler[core_id].lock);

	if (!scheduler[core_id].run_queue_head) {
		spin_release(&scheduler[core_id].lock);
		goto hibernate_cpu;
	}

	if (!scheduler[core_id].current)
		scheduler[core_id].current = scheduler[core_id].run_queue_head;

	scheduler_queue_t* current_node = scheduler[core_id].current;
	if (!current_node || !current_node->thread) {
		spin_release(&scheduler[core_id].lock);
		goto hibernate_cpu;
	}

	thread_t* thread = current_node->thread;
	thread_t* active_thread = current_core->active_thread;
	uint64_t current_tick = vxHPETGetMainCount();
	bool needs_context_switch = false;

	bool from_scheduler = false;
	if (reg->rsp >= scheduler[core_id].sched_stack &&
	    reg->rsp < scheduler[core_id].sched_stack + SCHEDULER_STACK_SIZE) {
		from_scheduler = true;
		if (active_thread && !active_thread->in_kernel_sleep &&
		    (active_thread->state == THREAD_STATE_READY || active_thread->state == THREAD_STATE_RUNNING)) {
			needs_context_switch = true;
		}
	}

	if (active_thread && !active_thread->has_update_run_time) {
		active_thread->last_run_time = current_tick;
		active_thread->has_update_run_time = true;
	}

	// serial2_printf("current thread id %d\n", thread->id);

	if (active_thread && active_thread->state == THREAD_STATE_RUNNING) {
		if (!from_scheduler) {
			vxSaveRegister(reg, &active_thread->reg);
			active_thread->fs_base = msrReadFSBase();
			active_thread->gs_base = msrReadKernelGSBase();
		}

		uint64_t elapsed_ns =
		    (current_tick - active_thread->last_run_time) *
		    vxHPETMinTickNs();

		if (ns2ms(elapsed_ns) > VOXIA_MAX_SCHEDULER_TIME_MS || needs_context_switch) {
			active_thread->total_run_time_ns += elapsed_ns;
			needs_context_switch = true;
			active_thread->has_update_run_time = false;
			active_thread->state = THREAD_STATE_READY;
		}
	} else if (active_thread &&
	           (active_thread->state == THREAD_STATE_BLOCKED ||
	            active_thread->state == THREAD_STATE_TERMINATED)) {
		
		uint64_t elapsed_ns =
		    (current_tick - active_thread->last_run_time) *
		    vxHPETMinTickNs();
		active_thread->total_run_time_ns += elapsed_ns;
		active_thread->has_update_run_time = false;
		if (active_thread->state == THREAD_STATE_TERMINATED) {
			LOG2_DEBUG("SCHEDULER",
			           "core %d terminated thread id %d", core_id,
			           active_thread->id);
			active_thread->state = THREAD_STATE_HAL;
			if (active_thread == current_node->thread) {
				vxDeatachFromScheduler(current_node, true);
			}
		}
		needs_context_switch = true;
		if (!scheduler[core_id].run_queue_head) {
			spin_release(&scheduler[core_id].lock);
			goto hibernate_cpu;
		}
	}

	if (!from_scheduler) {
		if (active_thread == nullptr ||
		    active_thread->state != THREAD_STATE_RUNNING) {
			needs_context_switch = true;
		} else if (active_thread != thread) {
			needs_context_switch = true;
		}
	}

	scheduler_queue_t* wakeup_search = scheduler[core_id].run_queue_head;
	if (wakeup_search) {
		do {
			thread_t* t = wakeup_search->thread;
			if (t && t->state == THREAD_STATE_BLOCKED && t->wakeup_time != 0) {
				if (current_tick >= t->wakeup_time) {
					t->state = THREAD_STATE_READY;
					t->wakeup_time = 0;
					t->wake_pending = true;
				}
			}
			wakeup_search = wakeup_search->next_queue;
		} while (wakeup_search && wakeup_search != scheduler[core_id].run_queue_head);
	}

	scheduler_queue_t* next_node = current_node;

	if (needs_context_switch) {
		scheduler_queue_t* start = current_node;
		if (!start) start = scheduler[core_id].run_queue_head;
		
		scheduler_queue_t* search = start ? start->next_queue : NULL;
		if (start && !search) search = scheduler[core_id].run_queue_head;
		
		scheduler_queue_t* found = NULL;

		if (from_scheduler && current_node && current_node->thread &&
		    (current_node->thread->state == THREAD_STATE_READY ||
		     current_node->thread->state == THREAD_STATE_RUNNING)) {
			found = current_node;
		} else if (search) {
			do {
				thread_t* t = search->thread;
				if (t && (t->state == THREAD_STATE_RUNNING ||
				          t->state == THREAD_STATE_READY)) {
					found = search;
					break;
				}
				search = search->next_queue;
				if (!search) search = scheduler[core_id].run_queue_head;
			} while (search != start);
		}

		if (!found) {
			if (heartbeat[core_id] % 1000 == 0) {
				serial2_printf("SCHED: core %d heartbeat %ld "
				               "(no ready threads)\n",
				               core_id, heartbeat[core_id]);
				scheduler_queue_t* q = scheduler[core_id].run_queue_head;
				if (q) {
					scheduler_queue_t* curr = q;
					do {
						serial2_printf("  -> Thread %d state: %d\n", curr->thread->id, curr->thread->state);
						curr = curr->next_queue;
					} while (curr && curr != q);
				}
			}
			
			if (active_thread && active_thread->state == THREAD_STATE_BLOCKED) {
				vxSaveRegister(reg, &active_thread->reg);
				
				// Hijack interrupt return frame to enter idle hibernation
				reg->rip = (uintptr_t)sch_idle_loop;
				reg->cs = 0x28; // Kernel CS
				reg->ss = 0x30; // Kernel DS/SS
				reg->rflags = 0x202; // Interrupts enabled
				reg->rsp = scheduler[core_id].sched_stack + SCHEDULER_STACK_SIZE - 16;
				
				current_core->active_thread = NULL;
				current_core->next_is_user = 0;
			}
			
			spin_release(&scheduler[core_id].lock);
			goto done_eoi;
		}

		next_node = found;
		scheduler[core_id].current = next_node;
	}

	thread_t* next_thread = next_node->thread;
	
	if (next_thread->id == 11) {
		serial2_printf("SCHEDULER: selecting thread 11\n");
	}
	auto next_proc = next_thread->process;

	/* Update core state ONLY when we have a thread to run */
	scheduler_resume_switch(next_thread, current_core);
	current_core->active_thread = next_thread;
	next_thread->current_core_id = core_id;
	current_core->next_is_user = (next_thread->flags & THREAD_USER) ? 1 : 0;

	if (next_thread->in_kernel_sleep) {
		set_tss_stack(core_id, next_thread->kernel_stack_top);
		current_core->kernel_rsp = next_thread->kernel_stack_top;

		msrSetFSBase(next_thread->fs_base);
		msrSetKernelGSBase(next_thread->gs_base);

		spin_release(&scheduler[core_id].lock);

		if (next_proc && next_proc->page && current_core->next_is_user)
			paging_reload(next_proc->page);
		else if (!current_core->next_is_user)
			paging_reload(paging_get_highest_page_map());

		apic_eoi();
		kernel_context_restore(next_thread->kernel_rsp);
		__builtin_unreachable();
	}

	if (next_thread->state == THREAD_STATE_READY) {
		// serial2_printf("thread %d ready to run\n", next_thread->id);
		next_thread->state = THREAD_STATE_RUNNING;
		set_tss_stack(core_id, next_thread->kernel_stack_top);
		current_core->kernel_rsp = next_thread->kernel_stack_top;

		if (next_thread->in_kernel_sleep) {
			reg->rip = (uintptr_t)kernel_context_restore;
			reg->cs = 0x28;
			reg->ss = 0x30;
			reg->rflags = 0x202;
			reg->rdi = next_thread->kernel_rsp;
			reg->rsp = next_thread->kernel_rsp;
		} else {
			vxRestoreRegister(reg, &next_thread->reg);
		}

		if ((next_thread->flags & THREAD_USER) == 0) {
			next_thread->gs_base = (uint64_t)&core_data[core_id];
		}

		msrSetFSBase(next_thread->fs_base);
		msrSetKernelGSBase(next_thread->gs_base);

		if (next_proc && next_proc->page &&
		    current_core->next_is_user) {
			paging_reload(next_proc->page);

		} else if (!current_core->next_is_user)
			paging_reload(paging_get_highest_page_map());

		spin_release(&scheduler[core_id].lock);
		goto done_eoi;

	} else if (next_thread->state == THREAD_STATE_RUNNING) {
		if (needs_context_switch &&
		    (next_thread != active_thread || from_scheduler)) {
			if (next_thread != active_thread) {
				// serial2_printf("SCHED: core %d switching "
				//                "thread %d -> %d\n",
				//                core_id,
				//                active_thread
				//                    ?
				//                    (uint32_t)active_thread->id
				//                    : 0,
				//                (uint32_t)next_thread->id);
			}
			if (next_thread->in_kernel_sleep) {
				reg->rip = (uintptr_t)kernel_context_restore;
				reg->cs = 0x28;
				reg->ss = 0x30;
				reg->rflags = 0x202;
				reg->rdi = next_thread->kernel_rsp;
				reg->rsp = next_thread->kernel_rsp;
			} else {
				vxRestoreRegister(reg, &next_thread->reg);
			}
			msrSetFSBase(next_thread->fs_base);
			msrSetKernelGSBase(next_thread->gs_base);
			set_tss_stack(core_id, next_thread->kernel_stack_top);
			current_core->kernel_rsp =
			    next_thread->kernel_stack_top;

			if (next_proc && next_proc->page &&
			    current_core->next_is_user)
				paging_reload(next_proc->page);
			else if (!current_core->next_is_user)
				paging_reload(paging_get_highest_page_map());
		}
	}

	if (needs_context_switch || !next_thread->has_update_run_time) {
		next_thread->last_run_time = vxHPETGetMainCount();
		next_thread->has_update_run_time = true;
	}

	spin_release(&scheduler[core_id].lock);

	if (next_thread && next_thread->signal) {
		uint64_t pending = __atomic_load_n(
		    &next_thread->signal->pending.__bits[0], __ATOMIC_ACQUIRE);
		uint64_t mask = __atomic_load_n(
		    &next_thread->signal->mask.__bits[0], __ATOMIC_ACQUIRE);
		uint64_t active_signals = pending & ~mask;
		if (active_signals) {
			for (int sig = 1; sig <= 64; sig++) {
				if (active_signals & SIGBIT(sig)) {
					__atomic_fetch_and(
					    &next_thread->signal->pending
					         .__bits[0],
					    ~SIGBIT(sig), __ATOMIC_RELEASE);

					sig_handle_ptr_t handler =
					    next_thread->signal
					        ->handler[sig - 1];

					if (handler) {
						serial2_printf(
						    "execut epending signal\n");
						handler(sig);
					}
				}
			}
		}
	}

hibernate_cpu:
	if (current_core && current_core->active_thread && 
	    current_core->active_thread->state == THREAD_STATE_BLOCKED) {
		
		vxSaveRegister(reg, &current_core->active_thread->reg);
		
		reg->rip = (uintptr_t)sch_idle_loop;
		reg->cs = 0x28; // Kernel CS
		reg->ss = 0x30; // Kernel DS
		reg->rflags = 0x202; // IF enabled
		reg->rsp = scheduler[core_id].sched_stack + SCHEDULER_STACK_SIZE - 16;
		
		current_core->active_thread = NULL;
		current_core->next_is_user = 0;
	}

done_eoi:
	serial2_flush();
}

KERNEL_API
void thread_block() {
	thread_t* thread = get_current_core_data()->active_thread;
	uint16_t core_id = thread->current_core_id;

	uintptr_t flags = irq_save();
	thread->in_kernel_sleep = true;

	thread->state = THREAD_STATE_BLOCKED;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);

	if (thread->wake_pending) {
		thread->wake_pending = false;
		thread->in_kernel_sleep = false;
		thread->state = THREAD_STATE_RUNNING;
		irq_restore(flags);
		return;
	}

	LOG2_DEBUG("scheduler", "thread block on thread %d (%d)", thread->id,
                   thread->process ? thread->process->pid : 0);

	thread->in_kernel_sleep = true;
	thread->fs_base = msrReadFSBase();
	thread->gs_base = msrReadKernelGSBase();

	get_current_core_data()->active_thread = NULL;

	extern void scheduler_resume_point(void);
	kernel_context_save(&thread->kernel_rsp, scheduler[core_id].sched_rsp,
	                    (uintptr_t)scheduler_resume_point);

	serial2_printf("return from thread block\n");
	thread->in_kernel_sleep = false;
	thread->wake_pending = false;
	thread->state = THREAD_STATE_RUNNING;
	irq_restore(flags);
}

KERNEL_API
void thread_sleep(uint64_t ms) {
	thread_t* thread = get_current_core_data()->active_thread;
	if (!thread) return;

	uint16_t core_id = thread->current_core_id;
	uintptr_t flags = irq_save();

	// Calculate wakeup time
	uint64_t current_tick = vxHPETGetMainCount();
	uint64_t ticks_to_wait = ms * 1000000 / vxHPETMinTickNs();
	thread->wakeup_time = current_tick + ticks_to_wait;

	thread->in_kernel_sleep = true;

	if (thread->wake_pending) {
		thread->wake_pending = false;
		thread->in_kernel_sleep = false;
		thread->wakeup_time = 0;
		irq_restore(flags);
		return;
	}

	thread->state = THREAD_STATE_BLOCKED;

	thread->fs_base = msrReadFSBase();
	thread->gs_base = msrReadKernelGSBase();

	get_current_core_data()->active_thread = NULL;

	extern void scheduler_resume_point(void);
	kernel_context_save(&thread->kernel_rsp, scheduler[core_id].sched_rsp,
	                    (uintptr_t)scheduler_resume_point);

	thread->in_kernel_sleep = false;
	thread->wake_pending = false;
	thread->wakeup_time = 0;
	thread->state = THREAD_STATE_RUNNING;
	irq_restore(flags);
}

KERNEL_API
void schedule_yield() {
	thread_t* thread = get_current_core_data()->active_thread;
	if (!thread)
		return;

	uint16_t core_id = thread->current_core_id;

	uintptr_t flags = irq_save();
	thread->in_kernel_sleep = true;

	if (thread->state != THREAD_STATE_TERMINATED) {
		thread->state = THREAD_STATE_READY;
	}

	thread->fs_base = msrReadFSBase();
	thread->gs_base = msrReadKernelGSBase();

	get_current_core_data()->active_thread = NULL;

	kernel_context_save(&thread->kernel_rsp, scheduler[core_id].sched_rsp,
	                    (uintptr_t)scheduler_resume_point);

	if (thread->state != THREAD_STATE_TERMINATED &&
	    thread->state != THREAD_STATE_HAL) {
		thread->in_kernel_sleep = false;
		thread->state = THREAD_STATE_RUNNING;
	}
	irq_restore(flags);
}

static void scheduler_resume_switch(thread_t* next_thread,
                                    each_core_data* current_core) {
	thread_t* prev = current_core->active_thread;
	
	uint64_t current_tick = vxHPETGetMainCount();
	if (prev && prev != next_thread && prev->has_update_run_time) {
		uint64_t elapsed_ns = (current_tick - prev->last_run_time) * vxHPETMinTickNs();
		prev->total_run_time_ns += elapsed_ns;
		prev->has_update_run_time = false;
	}

	if (next_thread && (!next_thread->has_update_run_time || next_thread != prev)) {
		next_thread->last_run_time = current_tick;
		next_thread->has_update_run_time = true;
	}

	// Eager FPU Save (save old thread's FPU state)
	if (prev && prev != next_thread && prev->fpu_state) {
		uint64_t cr4;
		__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
		if (cr4 & (1ULL << 18)) {
			__asm__ volatile("xor %%ecx, %%ecx\n\t"
			                 "xgetbv\n\t"
			                 "xsave64 (%0)"
			                 :
			                 : "r"(prev->fpu_state)
			                 : "eax", "ecx", "edx", "memory");
		} else {
			__asm__ volatile("fxsave64 (%0)"
			                 :
			                 : "r"(prev->fpu_state)
			                 : "memory");
		}
	}

	set_tss_stack(next_thread->current_core_id,
	              next_thread->kernel_stack_top);
	current_core->kernel_rsp = next_thread->kernel_stack_top;
	current_core->active_thread = next_thread;
	current_core->next_is_user = (next_thread->flags & THREAD_USER) ? 1 : 0;

	// Eager FPU Restore (load new thread's FPU state)
	if (next_thread->fpu_state) {
		uint64_t cr4;
		__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
		if (cr4 & (1ULL << 18)) {
			__asm__ volatile("xor %%ecx, %%ecx\n\t"
			                 "xgetbv\n\t"
			                 "xrstor64 (%0)"
			                 :
			                 : "r"(next_thread->fpu_state)
			                 : "eax", "ecx", "edx", "memory");
		} else {
			__asm__ volatile("fxrstor64 (%0)"
			                 :
			                 : "r"(next_thread->fpu_state)
			                 : "memory");
		}
	}

	msrSetFSBase(next_thread->fs_base);
	msrSetKernelGSBase(next_thread->gs_base);

	auto next_proc = next_thread->process;
	if (!next_proc) {
		paging_reload(paging_get_highest_page_map());
		return;
	}
	if (next_proc->page)
		paging_reload(next_proc->page);
}

__attribute__((noreturn)) void scheduler_resume_point(void) {
	uint16_t core_id = get_current_core_cpuid();
	// each_core_data* current_core = get_current_core_data();

	while (true) {
		uintptr_t flags = irq_save();
		spin_acquire(&scheduler[core_id].lock);

		scheduler_queue_t* start = scheduler[core_id].current;
		if (!start)
			start = scheduler[core_id].run_queue_head;

		scheduler_queue_t* node = start ? start->next_queue : NULL;
		if (!node)
			node = scheduler[core_id].run_queue_head;
			
		scheduler_queue_t* found = NULL;

		if (node) {
			scheduler_queue_t* original_node = node;
			do {
				thread_t* t = node->thread;
				if (t && (t->state == THREAD_STATE_RUNNING ||
				          t->state == THREAD_STATE_READY)) {
					found = node;
					break;
				}
				node = node->next_queue;
				if (!node) node = scheduler[core_id].run_queue_head;
			} while (node != original_node);
		}

		if (found) {
			scheduler[core_id].current = found;
		}
		spin_release(&scheduler[core_id].lock);
		irq_restore(flags);

		if (!found) {
			__asm__ volatile("sti\npause");
			continue;
		}

		thread_t* next = found->thread;
		
		if (next->id == 11) {
			serial2_printf("SCHED_RESUME: selecting thread 11\n");
		}

		auto current_core = get_current_core_data();
		scheduler_resume_switch(next, current_core);

		if (next->in_kernel_sleep) {
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

static scheduler_queue_t* vxAllocScheduler(const uint16_t core, thread_t* thr) {
	spin_acquire(&scheduler[core].lock);

	scheduler_queue_t* queue =
	    (scheduler_queue_t*)vxSlabAlloc(scheduler_cache);

	if (!queue) {
		spin_release(&scheduler[core].lock);
		return nullptr;
	}

	memset(queue, 0, sizeof(scheduler_queue_t));
	queue->thread = thr;

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

	uint16_t core = 0;
	if (new_thread->core_affinity != (uint16_t)-1) {
		core = new_thread->core_affinity;
	} else {
		uint16_t jum_core = vxGetActiveCoreCount();
		if (jum_core > 1) {
			static uint16_t next_core_hint = 1;
			// Dapatkan index core secara round-robin (skip index
			// 0/BSP)
			uint16_t core_idx =
			    (__atomic_fetch_add(&next_core_hint, 1,
			                        __ATOMIC_RELAXED) %
			     (jum_core - 1)) +
			    1;
			// Ambil APIC ID yang sebenarnya dari core index
			// tersebut
			auto cpu_info = vxGetCpuInfoByIndex((uint8_t)core_idx);
			core = cpu_info ? cpu_info->apicid : 0;
		} else {
			// Jika hanya ada 1 core aktif, kembalikan APIC ID dari
			// BSP
			auto cpu_info = vxGetCpuInfoByIndex(0);
			core = cpu_info ? cpu_info->apicid : 0;
		}
	}

	// core = core == 0 ? core + 1 : core;

	new_thread->core_affinity = core;
	new_thread->state = THREAD_STATE_READY;
	scheduler_queue_t* queue = vxAllocScheduler(core, new_thread);
	if (!queue) {
		LOG2_ERROR("SCHED", "error allocate scheduler for thread %d",
		           new_thread->id);
		return;
	}

	LOG2_DEBUG("SCHED", "ATTACH thread id %d -> queue 0x%lx (core %d)",
	           new_thread->id, queue, core);

	auto _queue = scheduler[core].run_queue_head;
	auto curr_queue = _queue;
	do {
		serial2_printf("thread id %d\n", curr_queue->thread->id);
		curr_queue = curr_queue->next_queue;
	} while (curr_queue != _queue);
}

KERNEL_API
boolean_t vxIsSchedulerRunning() {
	each_core_data* core = get_current_core_data();
	if (!core || !core->scheduler)
		return false;
	return core->scheduler->is_running && core->active_thread != NULL;
}

void vxStartScheduler(void) {
	const uint8_t core_id = get_current_core_cpuid();

	void* stack = kalloc(SCHEDULER_STACK_SIZE);
	if (!stack) {
		serial2_printf(
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

	serial2_printf("scheduler init on core %d 0x%x\n", core_id,
	               vxSchedulerTick);

	scheduler[core_id].is_running = true;

	auto irq_num = irq_alloc_entry(core_id);
	scheduler[core_id].timer_vector = irq_num;
	scheduler[core_id].tick_interval_us = 10000;

	irq_register(core_id, irq_num, (void*)vxSchedulerTick, true, 0x28, 0,
	             INTERRUPT_ATTR_KERNEL);
	vxAPICCreateTimer(APIC_TIMER_PERIOD,
	                  scheduler[core_id].tick_interval_us,
	                  scheduler[core_id].timer_vector);
}

KERNEL_API
void vxThreadWake(thread_t* thread) {
	if (!thread)
		return;

	thread->wake_pending = true;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
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

	// Detach from queue
	vxDeatachFromScheduler(current_node, true);

	// no another thread, just loop
	if (!scheduler[core_id].run_queue_head) {
		spin_release(&scheduler[core_id].lock);
		INFLOOP;
	}

	scheduler_queue_t* next_node = scheduler[core_id].current;
	if (!next_node)
		next_node = scheduler[core_id].run_queue_head;

	thread_t* next = next_node->thread;

	auto current_core = get_current_core_data();

	if (next->state == THREAD_STATE_READY) {
		next->state = THREAD_STATE_RUNNING;
		if ((next->flags & THREAD_USER) == 0) {
			next->gs_base = (uint64_t)&core_data[core_id];
		}
	}

	if (next->in_kernel_sleep) {
		rsp->rip = (uintptr_t)kernel_context_restore;
		rsp->cs = 0x28;
		rsp->ss = 0x30;
		rsp->rflags = 0x202;
		rsp->rdi = next->kernel_rsp;
		rsp->rsp = next->kernel_rsp;
	} else {
		// Restore FULL register context
		vxRestoreRegister(rsp, &next->reg);
	}
	msrSetFSBase(next->fs_base);
	msrSetKernelGSBase(next->gs_base);
	set_tss_stack(core_id, next->kernel_stack_top);
	current_core->kernel_rsp = next->kernel_stack_top;

	next->current_core_id = core_id;
	current_core->active_thread = next;
	current_core->next_is_user = (next->flags & THREAD_USER) ? 1 : 0;

	scheduler[core_id].current = next_node;

	spin_release(&scheduler[core_id].lock);

	auto next_proc = next->process;
	if (!next_proc) {
		paging_reload(paging_get_highest_page_map());
		return;
	}
	if (next_proc->page)
		paging_reload(next_proc->page);
}