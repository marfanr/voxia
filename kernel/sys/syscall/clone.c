#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "procc/process.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include <sys/syscall.h>

#define CLONE_VM      0x00000100
#define CLONE_FS      0x00000200
#define CLONE_FILES   0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD  0x00010000
#define CLONE_SYSVSEM 0x00040000
#define CLONE_SETTLS  0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID 0x01000000

extern int g_xsave_size;
extern thread_t* thrCreateInstance();
extern sig_han_t* alloc_sig_handle();

int syscall_clone(interrupt_stack_frame_t* rsp) {
	auto parent = get_current_core_data()->active_thread;
	if (!(parent->flags & THREAD_USER)) {
		return -1;
	}

	uint64_t flags = rsp->rdi;
	void* child_stack = (void*)rsp->rsi;
	int* parent_tid = (int*)rsp->rdx;
	int* child_tid = (int*)rsp->r10;
	void* tls = (void*)rsp->r8;

	if (!(flags & CLONE_THREAD)) {
		// Jika CLONE_THREAD tidak di set, biasanya ini adalah pemanggilan fork() standar via clone
		// (musl biasanya memanggil clone(SIGCHLD, 0) untuk fork).
		// Kita akan fallback ke fork_process.
		auto child_thr = fork_process(parent, rsp);
		if (!child_thr) {
			return -1;
		}

		interrupt_stack_frame_t* child_frame = 
			(interrupt_stack_frame_t*)((uintptr_t)rsp - parent->kernel_stack_base + child_thr->kernel_stack_base);
		child_frame->rax = 0; // return 0 untuk child

		// Flush TLB to ensure parent triggers COW faults on writes
		uintptr_t cr3;
		__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
		__asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");

		serial2_printf("CLONE PARENT PID: %d, CHILD PID: %d\n", parent->process->pid, child_thr->process->pid); return (int)child_thr->process->pid;
	}

	thread_t* child_thr = thrCreateInstance();
	memcopy(child_thr, parent, sizeof(thread_t));
	
	child_thr->id = thrAcquireNewSlot();
	vxUpdateThreadSlot(child_thr->id, child_thr);
	
	child_thr->clear_child_tid = nullptr;
	child_thr->wake_pending = false;
	child_thr->in_kernel_sleep = false;

	if (flags & CLONE_SIGHAND) {
		child_thr->signal = parent->signal;
	} else {
		child_thr->signal = alloc_sig_handle();
		if (parent->signal) {
			memcopy(child_thr->signal, parent->signal, sizeof(sig_han_t));
			memset(&child_thr->signal->pending, 0, sizeof(sigset_t));
		}
	}

	process_add_thread(parent->process, child_thr);
	
	if (flags & CLONE_CHILD_CLEARTID) {
		child_thr->clear_child_tid = (uint32_t*)child_tid;
	}
	if (flags & CLONE_PARENT_SETTID) {
		if (parent_tid) {
			*parent_tid = (int)child_thr->id;
		}
	}
	if (flags & CLONE_CHILD_SETTID) {
		if (child_tid) {
			*child_tid = (int)child_thr->id;
		}
	}

	if (flags & CLONE_SETTLS) {
		child_thr->fs_base = (uintptr_t)tls;
	} else {
		child_thr->fs_base = parent->fs_base;
	}
	child_thr->gs_base = parent->gs_base;

	child_thr->stack_top = (uintptr_t)child_stack;
	child_thr->stack_base = 0; 
	child_thr->kernel_stack_base = (uintptr_t)kalloc(0x4000);
	child_thr->kernel_stack_top = child_thr->kernel_stack_base + 0x4000;
	child_thr->kernel_rsp = (child_thr->kernel_stack_top & ~(uintptr_t)0xF) - 8;

	// Populate actual thread register state for scheduler READY state
	child_thr->reg.r15 = rsp->r15;
	child_thr->reg.r14 = rsp->r14;
	child_thr->reg.r13 = rsp->r13;
	child_thr->reg.r12 = rsp->r12;
	child_thr->reg.r11 = rsp->r11;
	child_thr->reg.r10 = rsp->r10;
	child_thr->reg.r9  = rsp->r9;
	child_thr->reg.r8  = rsp->r8;
	child_thr->reg.rbp = rsp->rbp;
	child_thr->reg.rdi = rsp->rdi;
	child_thr->reg.rsi = rsp->rsi;
	child_thr->reg.rdx = rsp->rdx;
	child_thr->reg.rcx = rsp->rcx;
	child_thr->reg.rbx = rsp->rbx;
	child_thr->reg.rax = 0;         // Return value for child

	child_thr->reg.rip = rsp->rip;
	child_thr->reg.cs  = rsp->cs;
	child_thr->reg.rflags = rsp->rflags;
	child_thr->reg.rsp = (uintptr_t)child_stack; // Child's user stack
	child_thr->reg.ss  = rsp->ss;

	child_thr->raw_fpu_ptr = (uint8_t*)kalloc((size_t)(g_xsave_size + 64));
	child_thr->fpu_state = (uint8_t*)(((uintptr_t)child_thr->raw_fpu_ptr + 63) & ~63ULL);
	memcopy(child_thr->fpu_state, parent->fpu_state, (size_t)g_xsave_size);

	child_thr->state = THREAD_STATE_READY;
	serial2_printf("clone: created thread %d in proc %d (tls 0x%x)\n", 
                   (int)child_thr->id, parent->process->pid, tls);
                   
	attach_to_scheduler(child_thr);

	return (int)child_thr->id;
}