// // DEPRECATED WILL BE REMOVED
#include "idt.h"
#include <dev/cpu/apic/apic.h>
// #include <dev/cpu/pic/pic.h>
// #include <firmw/ehci/ehci.h>
// #include <hal/ethernet/e1000/e1000.h>
// #include <libk/debug/debug.h>
// #include <libk/serial.h>
#include <libk/timer.h>
// #include <procc/scheduler.h>
// #include <procc/task.h>

// extern void syscall_interupt ();
// extern void *int_table[];
// extern void pit_interrupt ();

// static uint32_t time_ = 0;
// static idt_entry_t idt[256];
// static idt_ptr_t idt_ptr;

// void
// syscall_interrupt_handler (uint64_t x, const char *c)
// {
//     // uint64_t rax = 0;
//     // asm volatile("mov %%rax, %0" : "=r"(rax));
//     // serial_trace("syscall function %d \n", rax);

//     // if (rax == (uint64_t)0x1) {
//     //   serial_send_string(c);
//     // }
// }

// void
// idt_reload (idt_ptr_t idt_ptr)
// {
//     asm volatile("lidt %0" : : "m"(idt_ptr));
// }

// void
// idt_setup (void)
// {
//     for (int i = 0; i <= 255; i++)
//         idt[i] = add_idt_entry ((uint64_t)int_table[i], 0x28, 0, 0x8E);

//     pic_remap ();
//     KDEBUG (1, "Remap and disable PIC done");

//     idt[48] = add_idt_entry ((uint64_t)int_table[48], 0x28, 0, 0xEE);
//     idt[60] = add_idt_entry ((uint64_t)int_table[60], 0x28, 0, 0xEE);
//     idt[0x40] = add_idt_entry ((uint64_t)int_table[0x40], 0x28, 0, 0xEE);
//     // idt[0x73] = add_idt_entry((uint64_t)syscall_interrupt_handler, 0x28,
//     0,
//     // 0xEF);
//     idt[0x73] = add_idt_entry ((uint64_t)syscall_interupt, 0x28, 0, 0xEE);

//     // flush
//     idt_ptr.limit = 256 * sizeof (idt_entry_t) - 1;
//     idt_ptr.base = (uint64_t)&idt[0];
//     idt_reload (idt_ptr);
// }

// idt_entry_t
// add_idt_entry (void *offset, uint16_t selector, uint8_t ist, uint8_t
// type_attr)
// {
//     idt_entry_t entry;
//     entry.offset_low = (uint64_t)offset;
//     entry.offset_mid = (uint64_t)offset >> 16;
//     entry.offset_high = (uint64_t)offset >> 32;
//     entry.selector = selector;
//     entry.ist = ist;
//     entry.type_attr = type_attr;
//     entry.zero = 0;
//     return entry;
// }

// const char *exception_messages[] = {
//     "Division By Zero",
//     "Debug",
//     "Non Maskable Interrupt",
//     "Breakpoint",
//     "Into Detected Overflow",
//     "Out of Bounds",
//     "Invalid Opcode",
//     "No Coprocessor",

//     "Double Fault",
//     "Coprocessor Segment Overrun",
//     "Bad TSS",
//     "Segment Not Present",
//     "Stack Fault",
//     "General Protection Fault",

//     "Page Fault",
//     "reserved",
//     "x87 FPU Floating Point Error",
//     "Alignment Check",
//     "Machine Check",
//     "SIMD Floating Point Exception",

//     "reserved",
//     "reserved",
//     "reserved",
//     "reserved",
//     "reserved",
//     "reserved",
//     "reserved",
//     "reserved",
//     "reserved",
//     "reserved",
//     "reserved",
//     "reserved",

// };

void usleep(uint32_t time) {
  uint32_t curr_time = apic_read(0x390);
  while (apic_read(0x390) - curr_time < time)
    ;
}

// extern int saved_rax;

// extern bool is_running_program;
// extern uint64_t highest_loaded_task_addr;
// extern uint64_t low_loaded_task_addr;

// void
// task_switcher ()
// {
// }
// extern void jump_usermode (uintptr_t addr);
// extern void init_runtime ();
// extern bool g__scheduler__is__running;

// extern void
// int_handler (registers_t *rsp)
// {

//     if (is_running_program && rsp->int_no == 13)
//         {
//             // get  retruned value from elf as abi;;;;;;;;
//             serial_trace ("progr pid %d terminated on 0x%x\n",
//                           scheduler_get_current_process_pid (), rsp->rip);
//             int cur_pid = scheduler_get_current_process_pid ();
//             g__scheduler__is__running = false;
//             struct task *task = task_get (cur_pid);
//             serial_trace ("cur pid %d\n", task->pid);
//             // task_free (cur_pid);
//             task->state = TASK_TERMINATED;
//             // task
//             is_running_program = 0;
//             g__scheduler__is__running = true;
//             rsp->rip = 0x0240000000;
//             apic_eoi ();
//             return;
//         }

//     if (rsp->int_no <= 31)
//         {
//             KDEBUG (3, "Exception: %s", exception_messages[rsp->int_no]);
//             KDEBUG (2, "Error code: %d", rsp->err_code);
//             serial_trace ("exception on %d\n", rsp->int_no);
//             KDEBUG (1, "RIP: %x, CS: %x, RFLAGS: %x, SS: %x", rsp->rip,
//                     rsp->cs, rsp->rflags, rsp->ss);
//             KDEBUG (1, "RAX: %x, RBX: %x, RCX: %x, RDX: %x", rsp->rax,
//                     rsp->rbx, rsp->rcx, rsp->rdx);
//             KDEBUG (1, "RSI: %x, RDI: %x, RBP: %x, RSP: %x", rsp->rsi,
//                     rsp->rdi, rsp->rbp, rsp->rsp);
//             for (;;)
//                 ;
//         }
//     else
//         {

//             if (rsp->int_no == 60)
//                 {
//                     serial_send_string ("IRQ 10 detected\n");
//                     ehci_interrupt ();
//                     // serial_send_number(scancode, 16);
//                 }
//             else if (rsp->int_no == 0x69)
//                 {
//                     // syscall
//                     uint64_t rax = 0;
//                     asm volatile("mov %%rax, %0" : "=r"(rax));
//                     serial_trace ("syscall function %d : %d\n", rax,
//                     rsp->rax);

//                     if (rax & (uint64_t)0x1)
//                         {
//                             const char *txt;
//                             asm volatile("mov %%rsi, %0" : "=r"(txt));
//                             serial_send_string (txt);
//                         }
//                 }
//             else if (rsp->int_no == 48)
//                 {
//                     time_ += 1;
//                     if (time_ > __UINT32_MAX__)
//                         time_ = 0;

//                     // if (scheduler_is_running ())
//                     // {
//                     scheduler_tick (rsp);
//                     // }
//                 }
//             else if (rsp->int_no == 61)
//                 {
//                     // serial_send_string("IRQ 11  (ethernet) detected\n");
//                     e1000_irq ();
//                 }
//             // check error
//             // *(volatile uint32_t *)(local_apic_addr + 0xB0) = 0;
//             // *(volatile uint32_t *)(local_apic_addr + 0x420) = 0x2;

//             // EOI check
//             // // eoi
//             apic_eoi ();
//             // if (rsp->int_no >= 40)
//         }
// }

// extern void
// pit_handler ()
// {
//     serial_send_string ("pit\n");
//     // ++time;
//     apic_eoi ();
// }
