#include "./interrupt.h"
#include "autoconf.h"
#include "hal/apic/apic.h"
#include "hal/cpu/core.h"
#include "hal/cpu/spinlock.h"
#include "init/init.h"
#include "libk/type.h"
#include <hal/ethernet/e1000/e1000.h>
#include <hal/usb/ehci.h>
#include <libk/debug/debug.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/memory_utils.h>
#include <memory/vm_manager.h>
#include <procc/scheduler.h>
#include <procc/task.h>

static interrupt_per_core_data_t interrupt_per_core_data[VOXIA_MAX_CORE] = {0};

void
interrupt_io_wait()
{
    outb(0x80, 0);
}

static void
interrupt_pic_remap(void)
{
    // start initialization sequence
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    interrupt_io_wait();
    // set offsets
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    interrupt_io_wait();
    // set master-slave relationship
    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);
    interrupt_io_wait();
    // set 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // mematikan pic
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
void
interrupt_reload(interrupt_pointers_t *ptr, interrupt_entry_t *tbl)
{
    ptr->limit = MAX_INTERRUPTS * sizeof(interrupt_entry_t) - 1;
    ptr->base  = (uint64_t)&tbl[0];
    asm volatile("lidt %0" : : "m"(*ptr));
    asm volatile("sti");
}

extern void *int_table[];
extern void  syscall_interupt();

void
interrupt_register(interrupt_entry_t *entries, uint8_t n, void *handler, uint16_t selector,
                   uint8_t ist, uint8_t type_attr)
{
    entries[n].offset_low  = (uint64_t)handler;
    entries[n].selector    = selector;
    entries[n].ist         = ist;
    entries[n].type_attr   = type_attr;
    entries[n].offset_mid  = (uint64_t)handler >> 16;
    entries[n].offset_high = (uint64_t)handler >> 32;
    entries[n].zero        = 0;
}

void
irq_register(uint8_t core, uint8_t n, void *handler, boolean_t use_default_isr, uint16_t selector,
             uint8_t ist, uint8_t type_attr)
{
    interrupt_per_core_data[core].irq_entries[n].handler         = handler;
    interrupt_per_core_data[core].irq_entries[n].use_default_isr = use_default_isr;
    interrupt_per_core_data[core].irq_entries[n].configured      = true;

    if (use_default_isr)
    {
        interrupt_register(interrupt_per_core_data[core].interrupt_entries, n,
                           (void *)(uint64_t)int_table[n], selector, ist, type_attr);
    }
    else
    {
        interrupt_register(interrupt_per_core_data[core].interrupt_entries, n, handler, selector,
                           ist, type_attr);
    }
}

void
irq_setup(uint16_t core)
{
    for (int i = 0; i < MAX_INTERRUPTS; i++)
        interrupt_register(interrupt_per_core_data[core].interrupt_entries, i,
                           (void *)(uint64_t)int_table[i], 0x28, 0, INTERRUPT_ATTR_KERNEL);

    interrupt_pic_remap();
    interrupt_reload(&interrupt_per_core_data[core].interrupt_pointers,
                     interrupt_per_core_data[core].interrupt_entries);

    interrupt_register(interrupt_per_core_data[core].interrupt_entries, 0x73,
                       (void *)(uint64_t)syscall_interupt, 0x28, 0, INTERRUPT_ATTR_USER);
}

// Setup on BSP
INIT(Interrupt)
{
    coreUpdateGs(0);
    irq_setup(0);

    LOG_INFO("INTERRUPT", "interrupt initialized");
}

// default ISR
static const char *exception_messages[] = {
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

enum EXCEPTION_ID
{
    DIVIDE_BY_ZERO         = 0,
    DEBUG                  = 1,
    NON_MASKABLE_INTERRUPT = 2,
    BREAKPOINT             = 3,
    INTO_DETECTED_OVERFLOW = 4,
    OUT_OF_BOUNDS          = 5,
    INVALID_OPCODE         = 6,
    NO_COPROCESSOR         = 7,

    DOUBLE_FAULT                = 8,
    COPROCESSOR_SEGMENT_OVERRUN = 9,
    BAD_TSS                     = 10,
    SEGMENT_NOT_PRESENT         = 11,
    STACK_FAULT                 = 12,
    GENERAL_PROTECTION_FAULT    = 13,

    PAGE_FAULT                    = 14,
    X87_FPU_FLOATING_POINT_ERROR  = 16,
    ALIGNMENT_CHECK               = 17,
    MACHINE_CHECK                 = 18,
    SIMD_FLOATING_POINT_EXCEPTION = 19,
    VIRTUALIZATION_EXCEPTION      = 20,
    CONTORL_PROTECTION_EXCEPTION  = 21,

    HYPERVISOR_INJECTION_EXCEPTION = 28,
    VMM_COMMUNICATION_EXCEPTION    = 29,
    SECURITY_EXCEPTION             = 30
};

boolean_t        is_running_program = 0;
extern void      jump_usermode(uintptr_t addr);
extern void      init_runtime();
extern boolean_t g__scheduler__is__running;
extern void      timer_handle(void);
extern void      virtio_irq();
extern boolean_t elf_has_running;
extern uintptr_t rip_before_run_elf;

void
iddle()
{
    for (;;)
        ;
}

spinlock_t int_lock;

extern void
vxInterruptHandler(interrupt_stack_frame_t *rsp)
{
    uint16_t cpu_id;
    __asm__ volatile("movw %%gs:0, %0" : "=r"(cpu_id));

    uint64_t int_number = rsp->int_no;

    // handle exception
    const scheduler_queue_t *queue = vxSchedulerGetCurrentQueue(cpu_id);
    if (queue && int_number < 31)
    {
        LOG_ERROR("INTERRUPT", "An Error detected %s (%d) on thread id %d",
                  exception_messages[int_number], int_number, queue->thread->id);
        queue->thread->state = THREAD_STATE_TERMINATED;
        rsp->rip             = (uintptr_t)iddle;
        goto end;
    }
    // if (elf_has_running && int_number == GENERAL_PROTECTION_FAULT)
    // {
    //     LOG_INFO("ELF", "elf terminated detected...");
    //     for (;;)
    //         ;
    //     return;
    // }

    if (rsp->int_no <= 31)
    {
        spin_acquire(&int_lock);
        if (rsp->int_no != 14)
        {
            serial_trace("\n\n %s \nexception rip %x\n", exception_messages[rsp->int_no], rsp->rip);

            serial_trace("error code: 0b%b\n", rsp->err_code);
            serial_trace("rip: 0x%x\n", rsp->rip);
            serial_trace("rsp: 0x%x\n", rsp->rsp);
            serial_trace("rax: 0x%x\n", rsp->rax);
            serial_trace("rbx: 0x%x\n", rsp->rbx);

            INFLOOP;
        }
        else
        {
            uint64_t cr2 = 0;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            // virtual_memory *vma_tree = vma_tree_find(ALIGN_DOWN(cr2, 0x1000));

            // if (!vma_tree->start_address)
            // {
            serial_trace("page fault at 0x%x\n", cr2);
            serial_trace("\n\n %s \nexception on %d\n", exception_messages[rsp->int_no],
                         rsp->int_no);
            INFLOOP;
        }
        spin_release(&int_lock);
    }

    // LOG_INFO("INT", "trigerred at %d at core %d", int_number, cpu_id);
    irq_entry_t *irq = &interrupt_per_core_data[cpu_id].irq_entries[int_number];
    if (irq->configured)
    {
        if (irq->use_default_isr)
            ((void (*)(interrupt_stack_frame_t *))irq->handler)(rsp);
    }

end:
    apic_eoi();
}
