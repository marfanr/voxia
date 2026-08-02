#include "./interrupt.h"
#include "autoconf.h"
#include "console/console.h"
#include "hal/apic/apic.h"
#include "hal/cpu/msr.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "procc/process.h"
#include <hal/cpu/core.h>
#include <libk/debug/debug.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/phys_window.h>
#include <memory/vm_manager.h>
#include <procc/scheduler.h>
#include <spinlock.h>
#include <str.h>
#include <type.h>

static interrupt_per_core_data_t interrupt_per_core_data[VOXIA_MAX_CORE] = {0};

static void interrupt_io_wait() { outb(0x80, 0); }

static void interrupt_pic_remap(void) {
	outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
	outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
	interrupt_io_wait();
	outb(PIC1_DATA, 0x20);
	outb(PIC2_DATA, 0x28);
	interrupt_io_wait();
	outb(PIC1_DATA, 4);
	outb(PIC2_DATA, 2);
	interrupt_io_wait();
	outb(PIC1_DATA, ICW4_8086);
	outb(PIC2_DATA, ICW4_8086);
	outb(PIC1_DATA, 0xFF);
	outb(PIC2_DATA, 0xFF);
}

static void interrupt_reload(interrupt_pointers_t* ptr, interrupt_entry_t* tbl) {
	ptr->limit = MAX_INTERRUPTS * sizeof(interrupt_entry_t) - 1;
	ptr->base = (uint64_t)&tbl[0];
	asm volatile("lidt %0" : : "m"(*ptr));
	asm volatile("sti");
}

extern void* int_table[];
// extern void syscall_interupt();

static void interrupt_register(interrupt_entry_t* entries, int n, void* handler, int selector, uint8_t ist, uint8_t type_attr) {
	entries[n].offset_low = (uint16_t)((uint64_t)handler & 0xFFFF);
	entries[n].selector = (uint16_t)selector;
	entries[n].ist = ist;
	entries[n].type_attr = type_attr;
	entries[n].offset_mid = (uint16_t)((uint64_t)handler >> 16);
	entries[n].offset_high = (uint64_t)handler >> 32;
	entries[n].zero = 0;
}

void irq_register(uint8_t core, int n, void* handler, boolean_t use_default_isr, uint16_t selector, uint8_t ist, uint8_t type_attr) {

	irq_entry_t* entry = &interrupt_per_core_data[core].irq_entries[n];
	int slot = -1;

	for (;;) {
		uint8_t old = __atomic_load_n(&entry->mask, __ATOMIC_ACQUIRE);
		if (old == 0xFF)
			return;

		uint8_t free_mask = (uint8_t)~old;
		int e = __builtin_ctz(free_mask);
		uint8_t new = (uint8_t)(old | (1 << e));

		if (__atomic_compare_exchange_n(&entry->mask, &old, new, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
			slot = e;
			break;
		}
	}

	entry->handler[slot] = handler;
	__atomic_store_n(&entry->use_default_isr, use_default_isr, __ATOMIC_RELAXED);
	__atomic_store_n(&entry->configured, true, __ATOMIC_RELEASE);

	if (use_default_isr) {
		interrupt_register(interrupt_per_core_data[core].interrupt_entries, n, (void*)(uint64_t)int_table[n], selector, ist, type_attr);
	} else {
		interrupt_register(interrupt_per_core_data[core].interrupt_entries, n, handler, selector, ist, type_attr);
	}
}

uint16_t irq_alloc_entry(uint8_t core) {
	for (uint16_t i = 0x50; i < MAX_INTERRUPTS; i++) {
		irq_entry_t* entry = &interrupt_per_core_data[core].irq_entries[i];

		/* Coba klaim slot: configured harus masih false */
		boolean_t expected = false;
		if (__atomic_compare_exchange_n(&entry->allocated, &expected, true, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
			return i;
		}
	}
	return 0xFFFF;
}

void irq_setup(uint16_t core) {
	memset(&interrupt_per_core_data[core], 0, sizeof(interrupt_per_core_data_t));
	for (int i = 0; i < MAX_INTERRUPTS; i++)
		interrupt_register(interrupt_per_core_data[core].interrupt_entries, i, (void*)(uint64_t)int_table[i], 0x28, 0, INTERRUPT_ATTR_KERNEL);

	if (core == 0)
		interrupt_pic_remap();

	interrupt_reload(&interrupt_per_core_data[core].interrupt_pointers, interrupt_per_core_data[core].interrupt_entries);
}

INIT(Interrupt) {
	uint8_t bsp_id = (uint8_t)vxGetApicID();
	update_core_gs(bsp_id);
	irq_setup(bsp_id);
}

static const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "reserved",
    "x87 FPU Floating Point Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point Exception",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
};

enum EXCEPTION_ID {
	DIVIDE_BY_ZERO = 0,
	DEBUG = 1,
	NON_MASKABLE_INTERRUPT = 2,
	BREAKPOINT = 3,
	INTO_DETECTED_OVERFLOW = 4,
	OUT_OF_BOUNDS = 5,
	INVALID_OPCODE = 6,
	NO_COPROCESSOR = 7,
	DOUBLE_FAULT = 8,
	COPROCESSOR_SEGMENT_OVERRUN = 9,
	BAD_TSS = 10,
	SEGMENT_NOT_PRESENT = 11,
	STACK_FAULT = 12,
	GENERAL_PROTECTION_FAULT = 13,
	PAGE_FAULT = 14,
	X87_FPU_FLOATING_POINT_ERROR = 16,
	ALIGNMENT_CHECK = 17,
	MACHINE_CHECK = 18,
	SIMD_FLOATING_POINT_EXCEPTION = 19,
	VIRTUALIZATION_EXCEPTION = 20,
	CONTORL_PROTECTION_EXCEPTION = 21,
	HYPERVISOR_INJECTION_EXCEPTION = 28,
	VMM_COMMUNICATION_EXCEPTION = 29,
	SECURITY_EXCEPTION = 30
};

boolean_t is_running_program = 0;
extern void jump_usermode(uintptr_t addr);
extern void init_runtime();
extern void timer_handle(void);
extern void virtio_irq();
extern boolean_t elf_has_running;
extern uintptr_t rip_before_run_elf;

spinlock_t int_lock;

__attribute__((no_stack_protector)) extern void vxInterruptHandler(interrupt_stack_frame_t* rsp, fpu_state_t* fpu) {
	UNUSED(fpu);
	auto cpu = get_current_core_data();
	auto cpu_id = cpu->core_id;

	uint64_t int_number = rsp->int_no;

	if (int_number < 32) {
		uintptr_t cr2 = 0;

		if (int_number == PAGE_FAULT) {
			asm volatile("mov %%cr2, %0" : "=r"(cr2));
			auto err = rsp->err_code;

			if (cr2 >= 0xFFFF800000000000ULL && !(err & 1)) {
				paging_sync_kernel_entry(cr2);
				int level;
				uint64_t entry = paging_get_entry_ext(paging_get_highest_page_map(), cr2, &level);
				if (entry & PAGE_PRESENT) {
					goto end;
				}
			}

			if (err & (1 << 3)) {
				uint64_t efer = vxRDMSR(0xC0000080);
				serial2_printf("[DEBUG] RSVD Page Fault. EFER: 0x%lx\n", efer);
			}

			// Present (err & 1) and Write fault ((err >> 1) & 1)
			if ((err & 1) && ((err >> 1) & 1)) {
				const scheduler_queue_t* queue = vxSchedulerGetCurrentQueue(cpu_id);
				auto curr_proc = queue->thread->process;
				if (queue && queue->thread && curr_proc->page) {
					uint64_t entry = paging_get_entry(curr_proc->page, cr2);

					if ((entry & PAGE_PRESENT) && (entry & PAGE_COW)) {

						uintptr_t new_phys = (uintptr_t)phys_base_alloc(1);

						if (new_phys) {
							uintptr_t fault_page = cr2 & ~0xFFFULL;

							uintptr_t temp_vaddr;
							mem_create_physwindow(new_phys, &temp_vaddr, PHYS_WINDOW_FLAG_WRITE | PHYS_WINDOW_FLAG_LOCK);

							memcopy((void*)temp_vaddr, (void*)fault_page, BLOCK_SIZE);

							mem_release_physwindow(temp_vaddr);

							uint64_t new_flags = (entry & ~PAGE_PHYS_MASK & ~PAGE_COW) | PAGE_WRITABLE;

							paging_mmap(curr_proc->page, fault_page, new_phys, new_flags);

							INVLPG(fault_page);

							goto end;
						}
					} else {
						serial2_printf("[COW-DBG] write-fault cr2=0x%lx PTE=0x%lx "
						               "present=%d cow=%d writable=%d huge=%d\n",
						               cr2, entry, !!(entry & PAGE_PRESENT), !!(entry & PAGE_COW), !!(entry & PAGE_WRITABLE),
						               !!(entry & PAGE_HUGE));
					}
				}
			}

			serial2_printf("page fault 0x%lx\n", cr2);
			if (cr2 == 0x100029f5c) {
				uint64_t* ctx = (uint64_t*)0x1000B2D88;
				serial2_printf("MALLOC CTX: %lx %lx %lx %lx %lx %lx\n", ctx[0], ctx[1], ctx[2], ctx[3], ctx[4], ctx[5]);
			}
			serial2_printf("  err bits: present=%d write=%d user=%d nx=%d\n", err & 1, (err >> 1) & 1, (err >> 2) & 1, (err >> 4) & 1);

			if (get_current_core_data()->active_thread && get_current_core_data()->active_thread->process) {
				auto tree = get_current_core_data()->active_thread->process->vm_page->tree;
				if (tree) {
					// We can't easily traverse RBT without a helper, let's just print something else.
					// Or just let's not dump VMAs because it's too much work right now.
					// Actually, let's just print the base address of the executable and libc instead.
				}
			}
		}

		serial2_printf("\n\n[EXCEPTION] %s (vector %d)\n", exception_messages[int_number], int_number);
		serial2_printf("  rip=0x%lx  rsp=0x%lx  err=0x%lx  cr2=0x%lx\n", rsp->rip, rsp->rsp, rsp->err_code, cr2);
		serial2_printf("  rax=0x%lx  rbp=0x%lx  rcx=0x%lx  rdx=0x%lx\n", rsp->rax, rsp->rbp, rsp->rcx, rsp->rdx);

		// console_printf("\n\n[EXCEPTION] %s (vector %d)\n",
		//                exception_messages[int_number], int_number);
		// console_printf("  rip=0x%x  rsp=0x%x  err=0x%x  cr2=0x%x\n",
		//                rsp->rip, rsp->rsp, rsp->err_code, cr2);
		// console_printf("  rax=0x%x  rbp=0x%x  rcx=0x%x  rdx=0x%x\n",
		//                rsp->rax, rsp->rbp, rsp->rcx, rsp->rdx);

		const scheduler_queue_t* queue = vxSchedulerGetCurrentQueue(cpu_id);
		if (queue && queue->thread) {
			LOG2_ERROR("INTERRUPT",
			           "Exception %s on thread id %d at rip=0x%x "
			           "cr2=0x%x",
			           exception_messages[int_number], queue->thread->id, rsp->rip, cr2);
			auto thr = queue->thread;

			// Check if exception occurred in user space (Ring 3)
			if ((rsp->cs & 3) != 0) {
				int sig = SIGSEGV;
				if (int_number == 0) {
					sig = SIGFPE;
				} else if (int_number == 6) {
					sig = SIGILL;
				}

				sig_handle_ptr_t handler = 0;
				if (thr->signal) {
					handler = thr->signal->handler[sig - 1];
				}

				if (handler && (uintptr_t)handler != 1) {
					// Deliver signal to user space
					serial2_printf("EXCEPTION: Custom signal %d "
					               "handler %p restorer %p for thread "
					               "%d\n",
					               sig, handler, thr->signal->restorer[sig - 1], thr->id);
					vxSaveRegister(rsp, &thr->saved_reg);
					thr->has_saved_reg = true;

					uint64_t* user_sp = (uint64_t*)((rsp->rsp & ~15ULL) - 8);
					*user_sp = (uint64_t)thr->signal->restorer[sig - 1];

					rsp->rsp = (uint64_t)user_sp;
					rsp->rdi = (uint64_t)sig;
					rsp->rsi = 0;
					rsp->rdx = 0;
					rsp->rip = (uintptr_t)handler;
					goto end;
				} else {
					// Default action or ignore: Terminate
					// process
					thr->state = THREAD_STATE_TERMINATED;
					auto procc = thr->process;
					if (procc) {
						procc->exited = true;
						procc->exit_code = (int)(128 + sig);
						auto parent = find_process_by_pid(procc->parent_pid);
						if (parent && parent->main_thread) {
							serial2_printf("waking parent "
							               "process %d "
							               "(current %d) due "
							               "to exception "
							               "signal %d\n",
							               parent->pid, procc->pid, sig);
							vxThreadWake(parent->main_thread);
						}
					}
					sch_restore_to_next_thread(rsp, cpu_id);
					goto end;
				}
			} else {
				// Kernel space exception -> terminate thread
				// directly
				thr->state = THREAD_STATE_TERMINATED;
				auto procc = thr->process;
				if (procc) {
					procc->exited = true;
					procc->exit_code = (int)(128 + int_number);
					auto parent = find_process_by_pid(procc->parent_pid);
					if (parent && parent->main_thread) {
						serial2_printf("waking parent process %d "
						               "(current %d) due to "
						               "kernel exception\n",
						               parent->pid, procc->pid);
						vxThreadWake(parent->main_thread);
					}
				}
				sch_restore_to_next_thread(rsp, cpu_id);
				goto end;
			}
		}
		INFLOOP;
	}

	{

		irq_entry_t* irq = &interrupt_per_core_data[cpu_id].irq_entries[int_number];

		if (__atomic_load_n(&irq->configured, __ATOMIC_ACQUIRE)) {
			uint8_t m = __atomic_load_n(&irq->mask, __ATOMIC_ACQUIRE);
			while (m) {
				int i = __builtin_ctz(m);
				void* h = __atomic_load_n(&irq->handler[i], __ATOMIC_ACQUIRE);
				if (h)
					((void (*)(interrupt_stack_frame_t*))h)(rsp);
				m &= (m - 1);
			}
		}
	}

end:
	apic_eoi();
}