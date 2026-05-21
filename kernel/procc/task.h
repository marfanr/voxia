// #ifndef __PROCC__TASK_H__
// #define __PROCC__TASK_H__

// #include <hal/cpu/paging.h>
// #include <hal/cpu/register.h>

// typedef enum {
// 	TASK_RUNNING,
// 	TASK_READY,
// 	TASK_WAITING,
// 	TASK_SUSPENDED,
// 	TASK_TERMINATED
// } task_state_t;

// typedef enum {
// 	TASK_PRIORITY_LOW,
// 	TASK_PRIORITY_MEDIUM,
// 	TASK_PRIORITY_HIGH,
// 	TASK_PRIORITY_REALTIME
// } task_priority_t;

// typedef size_t pid_t;

// #define TASK_MAX 1024

// typedef struct {
// 	uint64_t rax, rbx, rcx, rdx, rsi, rdi, rflag, rsp, rip, rbp;
// 	uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
// } task_cpu_state_t;

// struct heap {
// 	struct heap* next;
// 	struct heap* prev;
// 	size_t size;
// 	int free;
// };

// struct program_paramater {
// 	uint64_t argc;
// 	char** argv;
// 	char** envp;
// };

// struct task {
// 	pid_t pid;
// 	char* name;
// 	uintptr_t entry;
// 	struct program_paramater param;
// 	uintptr_t stack;
// 	struct heap* heap;
// 	task_state_t state;
// 	task_priority_t priority;
// 	page_t page_root;
// 	task_cpu_state_t cpu_state;
// 	struct task* next;
// };

// void task_initialize();
// void task_add(char* name, uintptr_t entry, task_state_t state,
// 	      task_priority_t priority, page_t page_root, uintptr_t stack,
// 	      struct program_paramater param);
// struct task* task_get(pid_t pid);
// void task_free(pid_t pid);
// struct task* task_get_current();

// #endif // __PROCC__TASK_H__