#include "procc/thread.h"
#include "autoconf.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/phys_base_allocator.h"
#include "memory/slab.h"
#include "memory/vm_manager.h"
#include "scheduler.h"
#include "sys/sig.h"
#include <hal/cpu/core.h>
#include <str.h>

static struct slab_cache* thread_cache = nullptr;
static thread_bucket_t bucket = {0};
uintptr_t default_kernel_stack_base = 0;
uintptr_t default_kernel_stack_top = 0;

INIT(Thread) {
	vxCreateSlabCache(&thread_cache, "thread", sizeof(thread_t), 0, 0);
	default_kernel_stack_base = (uintptr_t)kalloc(0x4000);
	default_kernel_stack_top = default_kernel_stack_base + 0x4000;
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

static void vxUpdateThreadSlot(const thread_id id, thread_t* thr) {
	const uint32_t idx = THREAD_GET_ID(id);
	bucket.slot[idx].thread = thr;
}

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
		thr->reg.rsp = (thr->stack_top & ~(uint64_t)0xF) - 8;
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

	// sig
	thr->signal = alloc_sig_handle();
	
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

thread_t* fork(thread_t* parent, uintptr_t entry) {
	// TODO: for now fork only work with user thread
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

	auto fork_proc = create_process(parent->process->name, fork_thr);
	fork_thr->process = fork_proc;
	auto parent_proc = parent->process;
	fork_proc->parent_pid = parent_proc->pid;
	fork_proc->main_thread = fork_thr;
	fork_proc->page = fork_page;

	auto new_vma = create_vmm_page();
	auto new_fs_base = phys_base_alloc(1);
	auto new_fs_base_vaddr =
	    vma_lookup_free_vaddr(new_vma, VMA_REGION_A, 1);

	auto target_fs_base = parent->fs_base;

	vxMultipleMmap(fork_page, target_fs_base, (uint64_t)new_fs_base, 1,
	               0b111);
	vxMultipleMmap(parent_proc->page, new_fs_base_vaddr,
	               (uint64_t)new_fs_base, 1, 0b111);

	memcopy((void*)new_fs_base_vaddr, (void*)parent->fs_base, 4096);

	uintptr_t tid_off =
	    (uintptr_t)parent->clear_child_tid - parent->fs_base;
	serial2_printf("tid offset %x\n", tid_off);

	*((uint32_t*)(new_fs_base_vaddr + tid_off)) = (uint32_t)fork_thr->id;

	fork_thr->fs_base = target_fs_base;
	serial2_printf("parent fs %x gs %x \n", parent->fs_base,
	               parent->gs_base);

	fork_thr->entry_addr = entry;
	serial2_printf("forking thread %d (%d) with entry 0x%x\n", fork_thr->id,
	               fork_proc->pid, entry);

	auto parent_vma = parent->process->vm_page;
	serial2_printf("forking from process %d with vma tree root 0x%x\n",
	               parent->process->pid, parent_vma);

	if (vma_clone_cow(parent_vma, new_vma, (uintptr_t*)fork_page,
	                  (uintptr_t*)parent_proc->page) < 0) {
		LOG_ERROR("FORK", "failed to clone user-space VMAs (COW)");
		return nullptr;
	}
	fork_thr->process->vm_page = new_vma;

	fork_thr->stack_top = USER_STACK_VADDR;
	fork_thr->stack_base = USER_STACK_VADDR - USER_STACK_SIZE + 4096;
	fork_thr->kernel_stack_base = (uintptr_t)kalloc(0x4000);
	fork_thr->kernel_stack_top = fork_thr->kernel_stack_base + 0x4000;
	fork_thr->kernel_rsp =
	    (fork_thr->kernel_stack_top & ~(uintptr_t)0xF) - 8;

	fork_thr->reg.rax = 0;
	fork_thr->state = THREAD_STATE_READY;
	vxUpdateThreadSlot(fork_thr->id, fork_thr);
	attach_to_scheduler(fork_thr);

	return fork_thr;
}