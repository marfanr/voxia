#include <sys/syscall.h>
#include <sys/api.h>
#include <dev/cpu/apic/apic.h>
#include <dev/cpu/int/idt.h>
#include <dev/graphic/fb.h>
#include <hal/cpu/paging.h>
#include <libk/console/console.h>
#include <libk/debug/debug.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/phys_base_allocator.h>
#include <procc/scheduler.h>
#include <procc/task.h>
#include <procc/scheduler.h>
#include <vfs/vfs.h>

extern boolean_t g__scheduler__is__running;

void syscall(void *stack_adr) {
    unsigned long *stack = (unsigned long *)stack_adr;
    uint64_t       rax   = stack[0];
    uint64_t       rdi   = stack[1];
    uint64_t       rsi   = stack[2];
    uint64_t       rdx   = stack[3];
    uint64_t       rcx   = stack[4];
    uint64_t       r8    = stack[5];
    uint64_t       r9    = stack[6];

    uint64_t ret = 0;
    switch (rax) {
        case SYSCALL_WRITE:
            ret = sys_write(rdi, (const char *)rsi, rdx);
            break;

        case SYSCALL_READ:
            ret = sys_read(rdi, (char *)rsi, rdx);
            break;

        case SYSCALL_ALLOC:
            ret = (unsigned long)sys_alloc(rdi);
            break;

        case SYSCALL_API:
            ret = (unsigned long)sys_api(rdi, rsi);
            break;

        case SYSCALL_OPEN:
            ret = (unsigned long)sys_open((const char *)rdi, rsi);
            break;

        case SYSCALL_EXIT:
            sys_exit(rdi);
            break;

        case SYSCALL_FSTAT:
            ret = (unsigned long)fstat(rdi, (uint8_t *)rsi);
            break;

        default:
            break;
    }
    apic_eoi();

    asm volatile("movq %0, %%rax"
                 :
                 : "r"(ret));
    // paging_reload (task->page_root);
}

int sys_read(int descriptor, char *buffer, uint64_t length) {
    // serial_trace("sys_read fd %d on addr 0x%x \n", descriptor, buffer);
    if (!buffer) {
        return -1;
    }
    return vfs_read(descriptor, buffer, length);
    return 0;
}

void sys_exit(int exit_code) {
    serial_trace("\e[36mprogr pid %d terminated, with exit code %d\e[0m\n",
                 scheduler_get_current_process_pid(), exit_code);
    int cur_pid               = scheduler_get_current_process_pid();
    g__scheduler__is__running = 0;
    struct task *task         = task_get(cur_pid);
    serial_trace("cur pid %d\n", task->pid);
    task_free(cur_pid);
    task->state               = TASK_TERMINATED;
    g__scheduler__is__running = 1;
}

int fstat(int fd, uint8_t *buf) {
    // serial_trace("fstat fd %d on addr 0x%x \n", fd, buf);
    return vfs_fstat(fd, buf);
}

/**
 * @brief write to console or serial port
 *
 * @param descriptor
 * @param buffer
 * @param length
 */
uint64_t
sys_write(uint64_t descriptor, const char *buffer, uint64_t length) {
    // int          cur_pid = scheduler_get_current_process_pid();
    // struct task *task    = task_get(cur_pid);
    // serial_trace("buffer address : 0x%x\n",
    //              (uint64_t)buffer);
    // serial_trace("sys_write fd %d on addr 0x%x \n", descriptor, buffer);
    if (descriptor == 0) {
        for (uint64_t i = 0; i < length; i++) {
            serial_putc(buffer[i]);
        }
    } else if (descriptor == 1) {
        console_print(buffer, length);
    }
    return length;
}

uint64_t sys_open(const char *path, uint64_t flags) {
    char *path_ = (char *)(phys_base_alloc(1 + strlen(path) / 4096));
    memset(path_, 0, 1 + strlen(path) / 4096);
    memcopy(path_, path, strlen(path));
    int x = vfs_open(path_, 0);

    return x;
}

void sys_load(const char *path) {
}

uintptr_t
sys_alloc(uint64_t size) {
    uintptr_t buf = (uintptr_t)phys_base_alloc(size);
    // serial_trace("buf : 0x%x\n", buf);
    return buf;
}

#define SYS_API_GRAPHIC 0XAD73CEFF

uint64_t
sys_api(uint64_t identifier, int version) {
    if (identifier == SYS_API_GRAPHIC) {
        if (version == 0) {
            uint64_t addr = api_graphic();
            // serial_trace("api : 0x%x\n", addr);
            return addr;
        }
    }
    // ....
    return 0;
}

uint64_t
sys_sbrk(uint64_t size) {
    return 0;
}

uint64_t
sys_mmap(uint64_t addr, uint64_t length, uint64_t prot, uint64_t flags,
         uint64_t fd, uint64_t offset) {
    return 0;
}
