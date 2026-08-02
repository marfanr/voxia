#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include "sys/sig.h"
#include <str.h>
#include <sys/syscall.h>

int64_t syscall_rt_sigaction(int sig, const void* act, void* oact,
                             size_t sigsetsize) {
	auto thr = get_current_core_data()->active_thread;
	if (!thr) {
		return -ENOSYS;
	}

	if (sigsetsize != 8) {
		return -EINVAL;
	}

	if (thr->process) {
		paging_reload(thr->process->page);
	}

	// serial2_printf("sig_action on thread %d\n", thr->id);
	

	return sig_action(thr->signal, sig, (const struct k_sigaction*)act,
	                  (struct k_sigaction*)oact);
}
