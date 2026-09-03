#include "hal/cpu/core.h"
#include "hal/cpu/paging.h"
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

	if (thr->process) {
		paging_reload(thr->process->page);
	}

	uint64_t current_mask = __atomic_load_n(thr->signal->mask.__bits, __ATOMIC_RELAXED);

	if (oldset) {
		memcopy(oldset, &current_mask, (sigsetsize < sizeof(uint64_t)) ? sigsetsize : sizeof(uint64_t));
	}

	if (!set) {
		return 0;
	}

	serial2_printf("sig %d on %x (size %d)\n", how, set, sigsetsize);
	sigset_t set_ = {0};
	memcopy((void*)&set_, set, (sigsetsize < sizeof(sigset_t)) ? sigsetsize : sizeof(sigset_t));

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
		__atomic_store_n(thr->signal->mask.__bits, set_.__bits[0],
		                 __ATOMIC_RELAXED);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}