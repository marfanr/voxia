#ifndef __PROCC__PROCESS_H__
#define __PROCC__PROCESS_H__

#include <type.h>

#define MAX_PID_ALLOWED 4194304

typedef uint32_t pid_t;

struct process_node;
struct process_head {
	struct process_node* first;
};

struct process_node {
	struct process_node* next;
	struct process_node* prev;
};

struct thread;
typedef struct proccess {
	pid_t pid;
	pid_t parent_pid;

	char name[64];

	struct thread* main_thread;
	// file_descriptor_t fd;
	int exit_code;
	bool exited;

	struct process_node cache;
} __attribute__((aligned(64))) proccess_t;

struct thread;

pid_t alloc_pid();
void free_pid(pid_t pid);

proccess_t* create_process(char* name, struct thread* main_thread);
int execve(const char* path, char* const argv[], char* const envp[]);

#endif // __PROCC__PROCESS_H__