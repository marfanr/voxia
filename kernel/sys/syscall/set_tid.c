#include "hal/cpu/core.h"
#include "libk/serial.h"
#include <sys/syscall.h>

pid_t syscall_set_tid(uintptr_t tid) {
	auto thr = get_current_core_data()->active_thread;
	auto curr_thread = get_current_core_data()->active_thread;
	curr_thread->clear_child_tid =	 (uint32_t*)tid;
	serial2_printf("last tid %d\n", *curr_thread->clear_child_tid);
	*curr_thread->clear_child_tid = (uint32_t)thr->id;
	serial2_printf("given tid %d from 0x%x fs base 0x%x\n", thr->id, tid,  thr->fs_base);
	return (pid_t)thr->id;
}