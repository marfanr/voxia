#include "procc/thread.h"
#include "autoconf.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/slab.h"
#include "memory/vm_manager.h"
#include <procc/scheduler.h>
#include "string.h"
#include "sys/sig.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include <hal/cpu/core.h>
#include <str.h>
#include <sys/fd.h>

static struct slab_cache* thread_cache = nullptr;
static thread_bucket_t bucket = {0};
uintptr_t default_kernel_stack_base = 0;
uintptr_t default_kernel_stack_top = 0;

KERNEL_API thread_t* vxGetThreadByIndex(uint32_t idx) {
	if (idx >= VOXIA_MAX_NUMBER_THREAD) return nullptr;
	if (!bucket.slot[idx].used) return nullptr;
	return bucket.slot[idx].thread;
}

KERNEL_API uint64_t vxGetThreadTotalRunTime(thread_t* thread) {
	if (!thread) return 0;
	return thread->total_run_time_ns;
}

KERNEL_API uint16_t vxGetThreadCurrentCore(thread_t* thread) {
	if (!thread) return 0;
	return thread->current_core_id;
}

INIT(Thread) {
	vxCreateSlabCache(&thread_cache, "thread", sizeof(thread_t), 0, 0);
	default_kernel_stack_base = (uintptr_t)kalloc(0x4000);
	default_kernel_stack_top = default_kernel_stack_base + 0x4000;
}

thread_id thrAcquireNewSlot() {
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

thread_t* thrCreateInstance() {
	return (thread_t*)vxSlabAlloc(thread_cache);
}

void vxUpdateThreadSlot(const thread_id id, thread_t* thr) {
	const uint32_t idx = THREAD_GET_ID(id);
	bucket.slot[idx].thread = thr;
}

extern uint32_t g_xsave_size;

thread_t* create_thread(uintptr_t entry, uintptr_t stack_top,
                        uintptr_t stack_base, uint16_t core_affinity,
                        uint8_t priority, uint16_t flags) {

	if ((!stack_base || !stack_top) && (flags & THREAD_USER)) {
		LOG2_ERROR("THREAD", "user thread must have own user stack\n");
		return nullptr;
	}

	thread_t* thr = thrCreateInstance();
	if (!thr)
		return nullptr;
	memset(thr, 0, sizeof(thread_t));

	serial2_printf("created thread at 0x%x \n", thr);
	thr->id = thrAcquireNewSlot();
	thr->entry_addr = entry;
	thr->core_affinity = core_affinity;
	thr->priority = priority;
	thr->flags = flags;
	thr->stack_top = stack_top;
	thr->stack_base = stack_base;
	thr->state = THREAD_STATE_CREATE;

	if (!stack_base) {
		thr->stack_base = default_kernel_stack_base;
	}
	if (!stack_top) {
		thr->stack_top = default_kernel_stack_top;
	}

	if (flags & THREAD_USER) {
		thr->reg.rip = entry;
		thr->reg.rsp = stack_top;
		thr->reg.cs = 0x48 | 3;
		thr->reg.ss = 0x40 | 3;
		thr->reg.rflags = 0x202;
		thr->reg.rbp = 0;
	} else {
		thr->reg.rip = entry;
		thr->reg.cs = 0x28;
		thr->reg.ss = 0x30;
		thr->reg.rflags = 0x202;
		thr->reg.rbp = 0;
	}
	// kernel stack
	void* kstack = kalloc(0x4000);
	thr->kernel_stack_base = (uintptr_t)kstack;
	thr->kernel_stack_top = (uintptr_t)kstack + 0x4000;
	thr->kernel_rsp = (thr->kernel_stack_top & ~(uintptr_t)0xF) - 8;

	if (!(flags & THREAD_USER)) {
		thr->reg.rsp = thr->kernel_rsp;
	}

	// sig
	thr->signal = alloc_sig_handle();

	// fpu
	thr->raw_fpu_ptr = (uint8_t*)kalloc(g_xsave_size + 64);
	thr->fpu_state =
	    (uint8_t*)(((uintptr_t)thr->raw_fpu_ptr + 63) & ~(uintptr_t)63);
	memset(thr->fpu_state, 0, g_xsave_size);
	// Initialize default FPU (FCW) and SSE (MXCSR) control words to mask exceptions
	// FCW at offset 0: 0x037F
	thr->fpu_state[0] = 0x7F;
	thr->fpu_state[1] = 0x03;
	// MXCSR at offset 24: 0x1F80
	thr->fpu_state[24] = 0x80;
	thr->fpu_state[25] = 0x1F;
	thr->fpu_state[26] = 0x00;
	thr->fpu_state[27] = 0x00;

	vxUpdateThreadSlot(thr->id, thr);
	LOG2_DEBUG("THREAD", "created thread %d", thr->id);
	return thr;
}

void thread_exit() {
	const uint16_t core_id = get_current_core_cpuid();
	auto queue = vxSchedulerGetCurrentQueue(core_id);
	queue->thread->state = THREAD_STATE_TERMINATED;
	for (;;)
		__asm__ volatile("hlt");
}

thread_t* fork_process(thread_t* parent, interrupt_stack_frame_t* rsp) {
	if (!(parent->flags & THREAD_USER)) {
		LOG2_ERROR("THREAD", "now fork only working on user thread");
		return nullptr;
	}

	thread_t* fork_thr = thrCreateInstance();
	memcopy(fork_thr, parent, sizeof(thread_t));
	fork_thr->id = thrAcquireNewSlot();
	auto fork_page = paging_create_page_directory();
	paging_setup(fork_page);

	fork_thr->clear_child_tid = nullptr;
	fork_thr->wake_pending = false;
	fork_thr->in_kernel_sleep = false;

	fork_thr->signal = alloc_sig_handle();
	if (parent->signal) {
		memcopy(fork_thr->signal, parent->signal, sizeof(sig_han_t));
		memset(&fork_thr->signal->pending, 0, sizeof(sigset_t));
	}

	auto fork_proc = create_process(parent->process->name, fork_thr);
	auto parent_proc = parent->process;
	fork_proc->parent_pid = parent_proc->pid;
	fork_proc->pgid = parent_proc->pgid;
	fork_proc->main_thread = fork_thr;
	fork_proc->page = fork_page;
	
	if (fork_proc->cwd) {
		dentry_put(fork_proc->cwd);
	}
	fork_proc->cwd = parent_proc->cwd;
	if (fork_proc->cwd) {
		dentry_get(fork_proc->cwd);
	}

	uintptr_t tid_off = 0;
	size_t size_in_4kb = 1;
	auto target_fs_base = parent->fs_base;
	uintptr_t tls_vma_start = target_fs_base;
	
	if (parent->clear_child_tid) {
		auto vma = vma_find_contains(parent_proc->vm_page, (uintptr_t)parent->clear_child_tid);
		if (vma) {
			size_in_4kb = vma->length / PAGE_SIZE_4KB;
			if (size_in_4kb == 0) size_in_4kb = 1;
			tls_vma_start = vma->start_address;
			tid_off = (uintptr_t)parent->clear_child_tid - tls_vma_start;
		} else {
			/* Fallback if somehow not in VMA tree */
			tls_vma_start = ALIGN_DOWN((uintptr_t)parent->clear_child_tid, PAGE_SIZE_4KB);
			tid_off = (uintptr_t)parent->clear_child_tid - tls_vma_start;
		}
	}

	auto new_vma = create_vmm_page();
	auto new_fs_base = phys_base_alloc(size_in_4kb);

	auto new_fs_temporary_base_vaddr =
	    vma_lookup_free_vaddr(parent_proc->vm_page, VMA_REGION_PROCESS, size_in_4kb);

	paging_multiple_mmap(parent_proc->page, new_fs_temporary_base_vaddr,
	                     (uint64_t)new_fs_base, size_in_4kb,
	                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

	memcopy((void*)new_fs_temporary_base_vaddr, (void*)tls_vma_start,
	        size_in_4kb * PAGE_SIZE_4KB);

	if (parent->clear_child_tid) {
		*((uint32_t*)(new_fs_temporary_base_vaddr + tid_off)) = (uint32_t)fork_thr->id;
	}

	fork_thr->fs_base = target_fs_base;
	serial2_printf("parent fs %x gs %x \n", parent->fs_base,
	               parent->gs_base);

	fork_thr->entry_addr = rsp->rip;
	serial2_printf("forking thread %d (%d) with entry 0x%x\n", fork_thr->id,
	               fork_proc->pid, rsp->rip);

	auto parent_vma = parent->process->vm_page;
	serial2_printf("forking from process %d with vma tree root 0x%x\n",
	               parent->process->pid, parent_vma);

	if (vma_clone_cow(parent_vma, new_vma, (uintptr_t*)fork_page,
	                  (uintptr_t*)parent_proc->page) < 0) {
		LOG_ERROR("FORK", "failed to clone user-space VMAs (COW)");
		return nullptr;
	}

	if (parent->clear_child_tid) {
		paging_multiple_mmap(fork_page, tls_vma_start, (uint64_t)new_fs_base,
		                     size_in_4kb,
		                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
	}
	serial2_printf("Vma clow done\n");
	fork_thr->process->vm_page = new_vma;

	fork_thr->stack_top = USER_STACK_VADDR;
	fork_thr->stack_base = USER_STACK_VADDR - USER_STACK_SIZE + 4096;
	fork_thr->kernel_stack_base = (uintptr_t)kalloc(0x4000);
	fork_thr->kernel_stack_top = fork_thr->kernel_stack_base + 0x4000;
	fork_thr->kernel_rsp =
	    (fork_thr->kernel_stack_top - sizeof(interrupt_stack_frame_t));

	memcopy((void*)fork_thr->kernel_rsp, rsp,
	        sizeof(interrupt_stack_frame_t));

	fork_thr->reg.r15 = rsp->r15;
	fork_thr->reg.r14 = rsp->r14;
	fork_thr->reg.r13 = rsp->r13;
	fork_thr->reg.r12 = rsp->r12;
	fork_thr->reg.r11 = rsp->r11;
	fork_thr->reg.r10 = rsp->r10;
	fork_thr->reg.r9  = rsp->r9;
	fork_thr->reg.r8  = rsp->r8;
	fork_thr->reg.rbp = rsp->rbp;
	fork_thr->reg.rdi = rsp->rdi;
	fork_thr->reg.rsi = rsp->rsi;
	fork_thr->reg.rdx = rsp->rdx;
	fork_thr->reg.rcx = rsp->rcx;
	fork_thr->reg.rbx = rsp->rbx;
	fork_thr->reg.rax = 0;         // Child return value

	fork_thr->reg.rip = rsp->rip;
	fork_thr->reg.cs  = rsp->cs;
	fork_thr->reg.rflags = rsp->rflags;
	fork_thr->reg.rsp = rsp->rsp;
	fork_thr->reg.ss  = rsp->ss;
	fork_thr->state = THREAD_STATE_READY;

	// fpu
	fork_thr->raw_fpu_ptr = (uint8_t*)kalloc(g_xsave_size + 64);
	fork_thr->fpu_state =
	    (uint8_t*)(((uintptr_t)fork_thr->raw_fpu_ptr + 63) &
	               ~(uintptr_t)63);
	if (parent->fpu_state) {
		memcopy(fork_thr->fpu_state, parent->fpu_state, g_xsave_size);
	} else {
		memset(fork_thr->fpu_state, 0, g_xsave_size);
	}

	vxUpdateThreadSlot(fork_thr->id, fork_thr);

	// clone every fd
	fork_proc->fdtable = alloc_fdtable();
	auto new_fds = fork_proc->fdtable->fds;
	auto new_fd_flags = fork_proc->fdtable->fd_flags;
	memcopy(fork_proc->fdtable, parent_proc->fdtable,
                sizeof(struct fdtable));
	
	fork_proc->fdtable->fds = new_fds;
	fork_proc->fdtable->fd_flags = new_fd_flags;

	for (uint32_t i = 0; i < parent_proc->fdtable->max_fds; i++) {
		if (parent_proc->fdtable->fds[i]) {
			auto fd = parent_proc->fdtable->fds[i];
			__atomic_add_fetch(&fd->count.counter, 1, __ATOMIC_SEQ_CST);
			fork_proc->fdtable->fds[i] = fd;
			fork_proc->fdtable->fd_flags[i] = parent_proc->fdtable->fd_flags[i];
		} else {
			fork_proc->fdtable->fds[i] = nullptr;
		}
	}

	// dentry
	dentry_ptr cur_proc_dentry = 0;
	auto proc_str = str("/proc/");
	auto cur_proc_str = str_concat(proc_str, itoa(fork_proc->pid, 10, (char[32]){0}));
	str_release(proc_str);
	if (vxnamei(cur_proc_str->c_str, &cur_proc_dentry) != VFS_OK) {
		LOG2_ERROR("Thread",
		           "failed to create proc dentry for pid %d\n",
		           fork_proc->pid);
		str_release(cur_proc_str);
		return nullptr;
	}

	str_release(cur_proc_str);

	// clone every fd folder
	for (uint32_t i = 0; i < fork_proc->fdtable->next_fd; i++) {
		dentry_ptr fd_dentry = 0;
		if (resolve_dentry(itoa(i, 10, (char[32]){0}), cur_proc_dentry, &fd_dentry,
		                   CREATE_MISSING_ENTRY) != VFS_OK) {
			LOG_ERROR(
			    "Thread",
			    "failed create a dentry for fd %d on proc %d\n", i,
			    fork_proc->pid);
			break;
		}
		if (!fork_proc->fdtable->fds[i]) {
			dentry_put(fd_dentry);
			continue;
		}
		fd_dentry->vnode = fork_proc->fdtable->fds[i]->vnode;
		dentry_put(fd_dentry);
	}

	attach_to_scheduler(fork_thr);

	return fork_thr;
}

void destroy_thread(thread_t* thr) {
	if (!thr)
		return;

	// TODO: destroy llocated stack, vma, etc
	slab_free(thread_cache, thr);
}