#include "hal/cpu/core.h"
#include "libk/serial.h"
#include "sys/err_no.h"
#include "sys/sig.h"
#include <str.h>
#include <sys/syscall.h>

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int64_t syscall_rt_sigprocmask(int how, void* set, void* oldset,
                               size_t sigsetsize) {
	auto thr = get_current_core_data()->active_thread;
	if (!thr) {
		return -ENOSYS;
	}

	if (oldset) {
		memset(oldset, 0, sigsetsize);
	}

	serial2_printf("sig %d on %x (size %d)\n", how, set, sigsetsize);
	sigset_t set_ = {0};
	memcopy((void*)&set_, set, sigsetsize);

	switch (how) {
	case SIG_BLOCK:
		__atomic_fetch_or(thr->signal->mask.__bits, set_.__bits[0],
		                  __ATOMIC_RELAXED);
		break;

	case SIG_UNBLOCK:
		__atomic_fetch_and(thr->signal->mask.__bits, ~set_.__bits[0],
		                   __ATOMIC_RELAXED);
		break;

	case SIG_SETMASK:
		__atomic_fetch_and(thr->signal->mask.__bits, set_.__bits[0],
		                   __ATOMIC_RELAXED);
		break;
	default:
		break;
	}

	return 0;
}