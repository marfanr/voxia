#ifndef __PROCC__PROCESS_H__
#define __PROCC__PROCESS_H__

#include "sys/descriptor.h"
#include <type.h>

typedef uint32_t pid_t;

typedef struct proccess {
	pid_t pid;
	pid_t parent_pid;
	char name[16];
	// file_descriptor_t fd;

	struct proccess* next;
	struct proccess* prev;
} proccess_t;

int execve(const char* path, char* const argv[], char* const envp[]);

#endif // __PROCC__PROCESS_H__