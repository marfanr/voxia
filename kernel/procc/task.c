#include "task.h"
#include <libk/serial.h>
#include <libk/type.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

static struct task *g__task     = 0;
static int          g__curr_pid = 1;

void
task_initialize()
{
}

struct task *
task_get_current()
{
    return g__task;
}

void
task_add(char *name, uintptr_t entry, task_state_t state, task_priority_t priority,
         page_t page_root, uintptr_t stack, struct program_paramater param)
{
    struct task *task = (struct task *)phys_base_alloc(1);
    serial_trace("task address : 0x%x\n", task);
    task->name      = name;
    task->state     = state;
    task->priority  = priority;
    task->page_root = page_root;
    task->stack     = stack;
    task->entry     = entry;
    task->pid       = g__curr_pid++;
    task->next      = NULL;
    task->param     = param;
    serial_trace("task pid %d\n", task->pid);

    struct task *current = g__task;
    if (current == 0)
    {
        g__task = task;
        // serial_trace("task added %s\n", task->name);
        return;
    }

    // serial_trace("head task address : 0x%x\n", current);
    while (current->next != 0)
    {
        current = current->next;
    }
    current->next = task;
    // serial_trace("task added %s\n", task->name);
}

struct task *
task_get(int pid)
{
    struct task *current = g__task;
    while (current != 0 && current->pid != pid)
    {
        current = current->next;
    }
    return current;
}

void
task_free(int pid)
{
    struct task *current = g__task;
    struct task *prev    = NULL;
    while (current != 0 && current->pid != pid)
    {
        prev    = current;
        current = current->next;
    }
    prev->next = current->next;

    // TODO: freeing memory has been allocated for loading
    phys_base_free(current, 1);
}
