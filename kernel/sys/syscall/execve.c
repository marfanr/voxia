#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/process.h"
#include "str.h"
#include "string.h"
#include <sys/syscall.h>

#define MAX_ARG_COUNT 32
#define MAX_ARG_STRLEN 4096

static char** copy_string_array(char* const* arr) {
	if (!arr || (uintptr_t)arr < 4096)
		return NULL;
		
	int count = 0;
	while (count < MAX_ARG_COUNT) {
		// Prevent dereferencing obvious invalid pointers (like 0xC)
		if (arr[count] != NULL && (uintptr_t)arr[count] < 4096) {
			break;
		}
		if (!arr[count])
			break;
		count++;
	}

	char** new_arr = (char**)kalloc(((size_t)count + 1) * sizeof(char*));
	for (int i = 0; i < count; i++) {
		size_t len = 0;
		while (arr[i][len] != '\0' && len < MAX_ARG_STRLEN) {
			len++;
		}
		new_arr[i] = (char*)kalloc(len + 1);
		memcopy(new_arr[i], (void*)arr[i], len);
		new_arr[i][len] = '\0';
	}
	new_arr[count] = NULL;
	return new_arr;
}

static void free_string_array(char** arr) {
	if (!arr)
		return;
	for (int i = 0; arr[i]; i++) {
		kfree2(arr[i]);
	}
	kfree2(arr);
}

int syscall_execve(const char* path, char* const argv[], char* const envp[],
                   interrupt_stack_frame_t* rsp) {
	(void)path;
	(void)argv;
	(void)envp;
	(void)rsp;

	auto thread = get_current_core_data()->active_thread;
	if (!thread) {
		return -1;
	}

	auto proc = thread->process;
	if (!proc) {
		return -1;
	}

	char** safe_argv = copy_string_array(argv);
	char** safe_envp = copy_string_array(envp);
	kstring path_copy = str(path);

	for (int i = 0; safe_argv[i]; i++) {
		serial2_printf("argv[%d] %s\n", i, safe_argv[i]);
	}

	int ret = run_process_at_proc(path_copy->c_str, safe_argv, safe_envp,
	                              proc, rsp);

	free_string_array(safe_argv);
	free_string_array(safe_envp);
	str_release(path_copy);

	return ret < 0 ? -1 : 0;
	return -1;
}