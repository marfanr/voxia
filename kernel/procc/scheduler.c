#include "./scheduler.h"
#include "./task.h"
#include <dev/cpu/apic/apic.h>
#include <hal/cpu/paging.h>
#include <libk/serial.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

boolean_t g__scheduler__is__running = 0;
extern void jump_usermode(uintptr_t addr, uintptr_t stack);

static uint32_t g__scheduler__tick = 0;
static boolean_t g__scheduler_tick_is_running = 0;
static int g__cur_runnig_process = -1;
static uint32_t delay_ticks = 0;
extern boolean_t is_running_program;

int scheduler_get_current_process_pid() { return g__cur_runnig_process; }

struct task *current_task = 0;

void scheduler_init() {
  serial_trace("scheduler init\n");

  // run process pertama
  current_task = task_get(1);

  if (current_task == 0)
    return;

  current_task->state = TASK_SUSPENDED;
  g__scheduler__tick = apic_read(0x390);
  g__cur_runnig_process = current_task->pid;
  delay_ticks = apic_read(0x390);
  paging_reload(current_task->page_root);
  serial_trace("current task page root : 0x%x\n", current_task->page_root);
  // paging_debug(PHYS2VIRT(current_task->page_root), 0x1000);
  // paging_debug(PHYS2VIRT(current_task->page_root), 0x1033);
  serial_trace("\nscheduler init done\n");
  g__scheduler_tick_is_running = 0;
  is_running_program = 0;
  g__scheduler__is__running = 0;
  // set rdi and rsi from task para

  jump_usermode(current_task->entry, current_task->stack);
}

extern void user_switch(task_cpu_state_t *reg);

#define SEC 1000000
#define MS 1000

void scheduler_tick(registers_t *reg) {
  if (!g__scheduler__is__running)
    return;
  // serial_trace("scheduler tick %d\n", g__cur_runnig_process);

  if ((apic_read(0x390) - delay_ticks) < 100 * MS)
    return;

  // if ((apic_read(0x390) - g__scheduler__tick) < 4 * MS)
  //     return;

  // backup register
  current_task->state = TASK_SUSPENDED;
  current_task->cpu_state.rax = reg->rax;
  current_task->cpu_state.rbx = reg->rbx;
  current_task->cpu_state.rcx = reg->rcx;
  current_task->cpu_state.rdx = reg->rdx;
  current_task->cpu_state.rsi = reg->rsi;
  current_task->cpu_state.rdi = reg->rdi;
  current_task->cpu_state.rbp = reg->rbp;
  current_task->cpu_state.rsp = reg->rsp;
  current_task->cpu_state.rip = reg->rip;
  current_task->cpu_state.rflag = reg->rflags;

  // get next process
  struct task *next_task = current_task->next;
  if (next_task == 0) {
    next_task = task_get(1);
  }

  // restore register
  paging_reload((page_t)(uint64_t)next_task->page_root);
  if (next_task->state == TASK_TERMINATED) {
    // task_free (next_task->pid);
    next_task = task_get(1);
  }

  if (next_task->state == TASK_SUSPENDED) {
    next_task->state = TASK_RUNNING;
    g__cur_runnig_process = next_task->pid;
    is_running_program = 1;

    reg->rax = next_task->cpu_state.rax;
    reg->rbx = next_task->cpu_state.rbx;
    reg->rcx = next_task->cpu_state.rcx;
    reg->rdx = next_task->cpu_state.rdx;
    reg->rsi = next_task->cpu_state.rsi;
    reg->rdi = next_task->cpu_state.rdi;
    reg->rbp = next_task->cpu_state.rbp;
    reg->rsp = next_task->cpu_state.rsp;
    reg->rip = next_task->cpu_state.rip;
    reg->rflags = next_task->cpu_state.rflag;
  } else if (next_task->state == TASK_READY) {
    g__cur_runnig_process = next_task->pid;
    is_running_program = 1;
    next_task->state = TASK_RUNNING;
    reg->rip = next_task->entry;
    reg->rsp = next_task->stack;
  }

  current_task = next_task;

  // serial_trace("scheduler tick\n");

  g__scheduler__tick = apic_read(0x390);
}
