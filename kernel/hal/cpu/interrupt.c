#include "./interrupt.h"
#include "hal/cpu/paging.h"
#include <dev/cpu/apic/apic.h>
#include <hal/ethernet/e1000/e1000.h>
#include <hal/usb/ehci.h>
#include <libk/debug/debug.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <libk/timer.h>
#include <memory/memory_utils.h>
#include <memory/virtual_memory_allocator.h>
#include <procc/scheduler.h>
#include <procc/task.h>

static interrupt_entry_t    interrupt_table[MAX_INTERRUPTS];
static interrupt_pointers_t interrupt_pointers;
static uint32_t             time_ = 0;

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

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
void
interrupt_reload()
{
    interrupt_pointers.limit = MAX_INTERRUPTS * sizeof(interrupt_entry_t) - 1;
    interrupt_pointers.base  = (uint64_t)&interrupt_table[0];
    asm volatile("lidt %0" : : "m"(interrupt_pointers));
}

extern void *int_table[];
extern void  syscall_interupt();

void
interrupt_setup(void)
{
    for (int i = 0; i < MAX_INTERRUPTS; i++)
        interrupt_register(i, (void *)(uint64_t)int_table[i], 0x28, 0, INTERRUPT_ATTR_KERNEL);

    interrupt_pic_remap();

    interrupt_register(48, (void *)(uint64_t)int_table[48], 0x28, 0, 0xEE);
    interrupt_register(60, (void *)(uint64_t)int_table[60], 0x28, 0, 0xEE);
    interrupt_register(0x40, (void *)(uint64_t)int_table[0x40], 0x28, 0, 0xEE);

    interrupt_register(0x73, (void *)(uint64_t)syscall_interupt, 0x28, 0, 0xEE);
    interrupt_reload();
}

void
interrupt_register(uint8_t n, void *handler, uint16_t selector, uint8_t ist, uint8_t type_attr)
{
    interrupt_table[n].offset_low  = (uint64_t)handler;
    interrupt_table[n].selector    = selector;
    interrupt_table[n].ist         = ist;
    interrupt_table[n].type_attr   = type_attr;
    interrupt_table[n].offset_mid  = (uint64_t)handler >> 16;
    interrupt_table[n].offset_high = (uint64_t)handler >> 32;
    interrupt_table[n].zero        = 0;
}

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

boolean_t              is_running_program = 0;
extern void            jump_usermode(uintptr_t addr);
extern void            init_runtime();
extern boolean_t       g__scheduler__is__running;
extern virtual_memory *vma_tree_find(uintptr_t virt_addr);

extern void
interrupt_handler(interrupt_stack_frame_t *rsp)
{
    // paging_reload (paging_get_highest_page_map ());
    if (is_running_program && rsp->int_no == 13)
    {

        serial_trace("\e[36mprogr pid %d terminated on 0x%x\e[0m\n",
                     scheduler_get_current_process_pid(), rsp->rip);
        int cur_pid               = scheduler_get_current_process_pid();
        g__scheduler__is__running = 0;
        struct task *task         = task_get(cur_pid);
        serial_trace("cur pid %d\n", task->pid);
        task_free(cur_pid);

        // task
        task->state = TASK_TERMINATED;
        // is_running_program        = 0;
        g__scheduler__is__running = 1;
        rsp->rip                  = 0x0240000000;
        apic_eoi();
        serial_trace("jump to 0x%x\n", rsp->rip);
        return;
    }

    if (rsp->int_no <= 31)
    {
        if (rsp->int_no != 14)
        {
            serial_trace("\n\n %s \nexception on %d\n", exception_messages[rsp->int_no],
                         rsp->int_no);

            serial_trace("error code: 0b%b\n", rsp->err_code);
            serial_trace("rip: 0x%x\n", rsp->rip);
            serial_trace("rsp: 0x%x\n", rsp->rsp);
            serial_trace("rax: 0x%x\n", rsp->rax);
            serial_trace("rbx: 0x%x\n", rsp->rbx);

            for (;;)
                ;
        }
        else
        {
            uint64_t cr2 = 0;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            virtual_memory *vma_tree = vma_tree_find(ALIGN_DOWN(cr2, 0x1000));

            if (vma_tree)
            {
                serial_trace("vma tree found 0x%x\n", vma_tree->virt_address);
                paging_mmap_fill(paging_get_highest_page_map(), vma_tree->virt_address,
                                 vma_tree->phys_address, vma_tree->length, 0b11);
            }
            else
            {
                serial_trace("page fault at 0x%x\n", cr2);
                serial_trace("\n\n %s \nexception on %d\n", exception_messages[rsp->int_no],
                             rsp->int_no);
                for (;;)
                    ;
            }
        }
    }
    else
    {

        if (rsp->int_no == 60)
        {
            ioforge_usb_ehci_interrupt();
        }
        else if (rsp->int_no == 0x69)
        {
            // syscall
            uint64_t rax = 0;
            asm volatile("mov %%rax, %0" : "=r"(rax));
            serial_trace("syscall function %d : %d\n", rax, rsp->rax);

            if (rax & (uint64_t)0x1)
            {
                const char *txt;
                asm volatile("mov %%rsi, %0" : "=r"(txt));
                serial_send_string(txt);
            }
        }
        else if (rsp->int_no == 48)
        {

            time_ += 1;
            if (time_ > __UINT32_MAX__)
                time_ = 0;

            // if (scheduler_is_running ())
            // {
            scheduler_tick(rsp);
            // }
        }
        else if (rsp->int_no == 61)
        {
            // serial_send_string("IRQ 11  (ethernet) detected\n");
            // e1000_irq ();
        }

        apic_eoi();
    }
}
