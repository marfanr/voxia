#include "./interrupt.h"
#include "autoconf.h"
#include "console/console.h"
#include "hal/apic/apic.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include <hal/cpu/core.h>
#include <libk/debug/debug.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <memory/memory_utils.h>
#include <memory/vm_manager.h>
#include <memory/phys_base_allocator.h>
#include <procc/scheduler.h>
#include <procc/task.h>
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

static void interrupt_reload(interrupt_pointers_t* ptr,
                             interrupt_entry_t* tbl) {
	ptr->limit = MAX_INTERRUPTS * sizeof(interrupt_entry_t) - 1;
	ptr->base = (uint64_t)&tbl[0];
	asm volatile("lidt %0" : : "m"(*ptr));
	asm volatile("sti");
}

extern void* int_table[];
// extern void syscall_interupt();

static void interrupt_register(interrupt_entry_t* entries, int n, void* handler,
                               int selector, uint8_t ist, uint8_t type_attr) {
	entries[n].offset_low = (uint16_t)((uint64_t)handler & 0xFFFF);
	entries[n].selector = (uint16_t)selector;
	entries[n].ist = ist;
	entries[n].type_attr = type_attr;
	entries[n].offset_mid = (uint16_t)((uint64_t)handler >> 16);
	entries[n].offset_high = (uint64_t)handler >> 32;
	entries[n].zero = 0;

	// serial_printf("registered interrupt 0x%x\n", n);
	// serial_printf("low %x\n", entries[n].offset_low);
	// serial_printf("mid %x\n", entries[n].offset_mid);
	// serial_printf("high %x\n", entries[n].offset_high);
	// serial_printf("selector %x\n", entries[n].selector);
	// serial_printf("ist %x\n", entries[n].ist);
	// serial_printf("type_attr %x\n", entries[n].type_attr);
	// serial_printf("handler 0x%x", handler);
}

void irq_register(uint8_t core, int n, void* handler, boolean_t use_default_isr,
                  uint16_t selector, uint8_t ist, uint8_t type_attr) {

	irq_entry_t* entry = &interrupt_per_core_data[core].irq_entries[n];
	int slot = -1;

	for (;;) {
		uint8_t old = __atomic_load_n(&entry->mask, __ATOMIC_ACQUIRE);
		if (old == 0xFF)
			return;

		uint8_t free_mask = (uint8_t)~old;
		int e = __builtin_ctz(free_mask);
		uint8_t new = (uint8_t)(old | (1 << e));

		if (__atomic_compare_exchange_n(&entry->mask, &old, new, false,
		                                __ATOMIC_ACQ_REL,
		                                __ATOMIC_RELAXED)) {
			slot = e;
			break;
		}
	}

	entry->handler[slot] = handler;
	__atomic_store_n(&entry->use_default_isr, use_default_isr,
	                 __ATOMIC_RELAXED);
	__atomic_store_n(&entry->configured, true, __ATOMIC_RELEASE);

	if (use_default_isr) {
		interrupt_register(
		    interrupt_per_core_data[core].interrupt_entries, n,
		    (void*)(uint64_t)int_table[n], selector, ist, type_attr);
	} else {
		interrupt_register(
		    interrupt_per_core_data[core].interrupt_entries, n, handler,
		    selector, ist, type_attr);
	}
}

uint16_t irq_alloc_entry(uint8_t core) {
	for (uint16_t i = 0x50; i < MAX_INTERRUPTS; i++) {
		irq_entry_t* entry =
		    &interrupt_per_core_data[core].irq_entries[i];

		/* Coba klaim slot: configured harus masih false */
		boolean_t expected = false;
		if (__atomic_compare_exchange_n(&entry->allocated, &expected,
		                                true, false, __ATOMIC_ACQ_REL,
		                                __ATOMIC_RELAXED)) {
			return i;
		}
	}
	return 0xFFFF;
}

void irq_setup(uint16_t core) {
	memset(&interrupt_per_core_data[core], 0,
	       sizeof(interrupt_per_core_data_t));
	for (int i = 0; i < MAX_INTERRUPTS; i++)
		interrupt_register(
		    interrupt_per_core_data[core].interrupt_entries, i,
		    (void*)(uint64_t)int_table[i], 0x28, 0,
		    INTERRUPT_ATTR_KERNEL);

	if (core == 0)
		interrupt_pic_remap();

	interrupt_reload(&interrupt_per_core_data[core].interrupt_pointers,
	                 interrupt_per_core_data[core].interrupt_entries);

	// interrupt_register(interrupt_per_core_data[core].interrupt_entries,
	// 		   0x73, (void*) (uint64_t) syscall_interupt, 0x28, 0,
	// 		   INTERRUPT_ATTR_USER);
}

INIT(Interrupt) {
	update_core_gs(0);
	irq_setup(0);
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
extern boolean_t g__scheduler__is__running;
extern void timer_handle(void);
extern void virtio_irq();
extern boolean_t elf_has_running;
extern uintptr_t rip_before_run_elf;

spinlock_t int_lock;

__attribute__((no_stack_protector)) extern void
vxInterruptHandler(interrupt_stack_frame_t* rsp, fpu_state_t* fpu) {
	UNUSED(fpu);
	auto cpu = get_current_core_data();
	auto cpu_id = cpu->core_id;

	uint64_t int_number = rsp->int_no;

	if (int_number < 32) {
		uintptr_t cr2 = 0;

		if (int_number == PAGE_FAULT) {
			asm volatile("mov %%cr2, %0" : "=r"(cr2));
			auto err = rsp->err_code;

			// Present (err & 1) and Write fault ((err >> 1) & 1)
			if ((err & 1) && ((err >> 1) & 1)) {
				const scheduler_queue_t* queue = vxSchedulerGetCurrentQueue(cpu_id);
				if (queue && queue->thread && queue->thread->page) {
					thread_t* thr = queue->thread;
					uint64_t entry = paging_get_entry(thr->page, cr2);

					// Check if page is Present and COW (bit 9 is 0x200)
					if ((entry & 1) && (entry & 0x200ULL)) {
						uintptr_t new_phys = (uintptr_t)phys_base_alloc(1);
						if (new_phys) {
							uintptr_t fault_page = cr2 & ~0xFFFULL;

							// Map new physical page temporarily into kernel virtual space
							uintptr_t temp_vaddr = vma_lookup_free_vaddr(get_kernel_vmm_page(), VMA_REGION_A, 1);
							vxMmap(paging_get_highest_page_map(), temp_vaddr, new_phys, 0b111);

							// Copy page contents
							memcopy((void*)temp_vaddr, (void*)fault_page, 4096);

							// Unmap temp kernel mapping
							paging_unmap_page(paging_get_highest_page_map(), temp_vaddr);

							// Map new page in thread PML4 as User | Write | Present (0b111)
							vxMmap(thr->page, fault_page, new_phys, 0b111);

							// Invalidate TLB for the faulting page
							asm volatile("invlpg (%0)" ::"r"(fault_page) : "memory");

							// Return immediately to re-execute the write instruction
							return;
						}
					}
				}
			}

			serial2_printf("page fault 0x%x\n", cr2);
			serial2_printf(
			    "  err bits: present=%d write=%d user=%d nx=%d\n",
			    err & 1, (err >> 1) & 1, (err >> 2) & 1,
			    (err >> 4) & 1);
		}

		serial2_printf("\n\n[EXCEPTION] %s (vector %d)\n",
		               exception_messages[int_number], int_number);
		serial2_printf("  rip=0x%x  rsp=0x%x  err=0x%x  cr2=0x%x\n",
		               rsp->rip, rsp->rsp, rsp->err_code, cr2);
		serial2_printf("  rax=0x%x  rbp=0x%x  rcx=0x%x  rdx=0x%x\n",
		               rsp->rax, rsp->rbp, rsp->rcx, rsp->rdx);

		// console_printf("\n\n[EXCEPTION] %s (vector %d)\n",
		//                exception_messages[int_number], int_number);
		// console_printf("  rip=0x%x  rsp=0x%x  err=0x%x  cr2=0x%x\n",
		//                rsp->rip, rsp->rsp, rsp->err_code, cr2);
		// console_printf("  rax=0x%x  rbp=0x%x  rcx=0x%x  rdx=0x%x\n",
		//                rsp->rax, rsp->rbp, rsp->rcx, rsp->rdx);

		const scheduler_queue_t* queue =
		    vxSchedulerGetCurrentQueue(cpu_id);
		if (queue && queue->thread) {
			LOG2_ERROR("INTERRUPT",
			           "Exception %s on thread id %d at rip=0x%x "
			           "cr2=0x%x",
			           exception_messages[int_number],
			           queue->thread->id, rsp->rip, cr2);
			queue->thread->state = THREAD_STATE_TERMINATED;
			sch_restore_to_next_thread(rsp, cpu_id);
		} else {

			INFLOOP;

			/* Tidak ada thread — terjadi di konteks kernel awal,
			 * tidak bisa di-recover, halt. */
		}

		goto end;
	}

	{

		irq_entry_t* irq =
		    &interrupt_per_core_data[cpu_id].irq_entries[int_number];

		if (__atomic_load_n(&irq->configured, __ATOMIC_ACQUIRE)) {
			uint8_t m =
			    __atomic_load_n(&irq->mask, __ATOMIC_ACQUIRE);
			while (m) {
				int i = __builtin_ctz(m);
				void* h = __atomic_load_n(&irq->handler[i],
				                          __ATOMIC_ACQUIRE);
				if (h)
					((void (*)(interrupt_stack_frame_t*))h)(
					    rsp);
				m &= (m - 1);
			}
		}
	}

	end:
		apic_eoi();
	}