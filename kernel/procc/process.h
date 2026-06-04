#ifndef __PROCC__PROCESS_H__
#define __PROCC__PROCESS_H__

#include "memory/vm_manager.h"
#include "spinlock.h"
#include "sys/sig.h"
#include <type.h>

#define MAX_PID_ALLOWED 4194304
#define INVALID_PID ((pid_t) - 1)

typedef uint32_t pid_t;

struct process_node;
struct process_head {
	struct process_node* first;
};

struct process_node {
	struct process_node* next;
	struct process_node* prev;
};

// TODO: move paging page information into here
struct thread;
typedef struct process {
	pid_t pid;
	pid_t parent_pid;
	volatile uintptr_t* page;
	struct thread* main_thread;
	struct fdtable* fdtable;
	int exit_code;
	bool exited;
	struct process_node cache;
	struct virtual_memory_page* vm_page;
	
	char name[64];
	
	sig_han_t* signal;
	uintptr_t heap_start;
	spinlock_t lock;
	uintptr_t heap_end;

	// linked list
	struct process* next;
	struct process* prev;
} __attribute__((aligned(64))) process_t;

struct thread;

pid_t alloc_pid();
void free_pid(pid_t pid);

process_t* create_process(char* name, struct thread* main_thread);
int run_process(const char* path, char* const argv[], char* const envp[]);
#include "hal/cpu/interrupt.h"
int run_process_at_proc(const char* path, char* const argv[],
                        char* const envp[], process_t* proc, interrupt_stack_frame_t* rsp);
process_t* find_process_by_pid(pid_t pid);

#endif // __PROCC__PROCESS_H__