#ifndef __SYS__SIG_H__
#define __SYS__SIG_H__

#include <type.h>

#define SIGSIZE 128 / sizeof(long)

typedef struct __sigset_t {
	unsigned long __bits[SIGSIZE];
} sigset_t;

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT SIGABRT
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGPOLL 29
#define SIGPWR 30
#define SIGSYS 31

#define SA_RESTART 0x10000000

#define SIGBIT(x) (1ULL << (x - 1))
#define MAX_SIGNAL_AVAILABLE 64

typedef void (*sig_handle_ptr_t)(int sig_num);

// musl
struct k_sigaction {
	void (*handler)(int);
	unsigned long flags;
	void (*restorer)(void);
	unsigned int mask[2];
};

typedef struct sig_han {
	sigset_t pending;
	sigset_t mask;
	sigset_t avail_handler;
	sig_handle_ptr_t handler[MAX_SIGNAL_AVAILABLE];
	unsigned long flags[MAX_SIGNAL_AVAILABLE];
	void (*restorer[MAX_SIGNAL_AVAILABLE])(void);
	uint64_t signal_mask[MAX_SIGNAL_AVAILABLE];
} __attribute__((aligned(64))) sig_han_t;

sig_han_t* alloc_sig_handle(void);
void free_sig_handle(sig_han_t* handle);
void sig_send(sig_han_t* handle, int sig);
void sig_wait(sig_han_t* handle, uint64_t mask);
void sig_register_handler(sig_han_t* handle, int sig,
                          sig_handle_ptr_t handler);
int sig_action(sig_han_t* handle, int sig, const struct k_sigaction* act,
               struct k_sigaction* oact);

#endif // __SYS__SIG_H__