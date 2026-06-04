#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "procc/process.h"
#include "procc/scheduler.h"
#include "sys/err_no.h"
#include <sys/syscall.h>

int syscall_execve(const char* path, char* const argv[], char* const envp[], interrupt_stack_frame_t* rsp) {
	(void)argv;
	auto thread = get_current_core_data()->active_thread;
	if (!thread) {
		return -1;
	}

	auto proc = thread->process;
	if (!proc) {
		return -1;
	}
    
	int ret = run_process_at_proc(path, argv, envp, proc, rsp);
	if (ret < 0) return ret;


	return 0;
}