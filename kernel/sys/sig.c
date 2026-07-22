#include "init/init.h"
#include "libk/serial.h"
#include "memory/slab.h"
#include "procc/scheduler.h"
#include "sys/err_no.h"
#include <str.h>
#include <sys/sig.h>

static struct slab_cache* sig_handle_cache = 0;

INIT(Sig) {
	vxCreateSlabCache(&sig_handle_cache, "sig_handle", sizeof(sig_han_t),
	                  64, 0);
}

sig_han_t* alloc_sig_handle(void) {
	auto sig = (sig_han_t*)vxSlabAlloc(sig_handle_cache);
	memset(sig, 0, sizeof(sig_han_t));
	return sig;
}

void free_sig_handle(sig_han_t* handle) {
	slab_free(sig_handle_cache, handle);
}

void sig_send(sig_han_t* handle, int sig) {
	if (sig < 1 || sig > MAX_SIGNAL_AVAILABLE) {
		serial2_printf("invalid signal %d\n", sig);
		return;
	}

	if (__atomic_load_n(&handle->mask.__bits[0], __ATOMIC_ACQUIRE) &
	    SIGBIT(sig)) {
		serial2_printf("signal %d is masked, ignored\n", sig);
		return;
	}

	sig_handle_ptr_t handler = handle->handler[sig - 1];

	serial2_printf("sigint handler: %p\n", handler);

	if (handler && (uintptr_t)handler >= 0xFFFF800000000000ULL) {
		handler(sig);
	} else {
		__atomic_fetch_or(&handle->pending.__bits[0], SIGBIT(sig),
		                  __ATOMIC_SEQ_CST);
	}
}

void sig_wait(sig_han_t* handle, uint64_t mask) {
	while ((__atomic_load_n(&handle->pending.__bits[0], __ATOMIC_ACQUIRE) &
	        mask) == 0) {
		thread_block();
	}

	__atomic_fetch_and(&handle->pending.__bits[0], ~mask, __ATOMIC_RELEASE);
}

void sig_register_handler(sig_han_t* handle, int sig,
                          sig_handle_ptr_t handler) {
	uint64_t prev = __atomic_fetch_or(&handle->mask.__bits[0], SIGBIT(sig),
	                                  __ATOMIC_RELAXED);

	if (!(prev & SIGBIT(sig))) {

		__atomic_store_n(&handle->handler[sig - 1], handler,
		                 __ATOMIC_RELEASE);

		uint64_t pending = __atomic_load_n(&handle->pending.__bits[0],
		                                   __ATOMIC_ACQUIRE);
		if (pending & SIGBIT(sig)) {

			__atomic_fetch_and(&handle->pending.__bits[0],
			                   ~SIGBIT(sig), __ATOMIC_RELEASE);
			handler(sig);
		}
	}
}

int sig_action(sig_han_t* handle, int sig, const struct k_sigaction* act,
               struct k_sigaction* oact) {
	if (sig < 1 || sig > MAX_SIGNAL_AVAILABLE) {
		return -EINVAL;
	}

	if (oact) {
		oact->handler = handle->handler[sig - 1];
		oact->flags = handle->flags[sig - 1];
		oact->restorer = handle->restorer[sig - 1];
		oact->mask[0] = (unsigned int)(handle->signal_mask[sig - 1] &
		                               0xFFFFFFFFULL);
		oact->mask[1] =
		    (unsigned int)((handle->signal_mask[sig - 1] >> 32) &
		                   0xFFFFFFFFULL);
	}

	if (act) {
		if (sig == 9 || sig == 19) { // SIGKILL = 9, SIGSTOP = 19
			return -EINVAL;
		}

		handle->handler[sig - 1] = act->handler;
		handle->flags[sig - 1] = act->flags;
		handle->restorer[sig - 1] = act->restorer;

		uint64_t mask_val =
		    ((uint64_t)act->mask[1] << 32) | act->mask[0];
		handle->signal_mask[sig - 1] = mask_val;
	}

	return 0;
}