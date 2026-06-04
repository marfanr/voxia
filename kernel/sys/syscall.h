#ifndef __SYS__SYSCALL_H__
#define __SYS__SYSCALL_H__

#include "hal/cpu/interrupt.h"
#include "procc/process.h"
#include <type.h>

// unix/linux compatible
#define SYSCALL_READ 0X0
#define SYSCALL_WRITE 0X1
#define SYSCALL_OPEN 0X2
#define SYSCALL_CLOSE 0x3
#define SYSCALL_FSTAT 0X4
#define SYSCALL_ALLOC 0x8
#define SYSCALL_ARCH_PRCTL 158
#define SYSCALL_API 0X9F
#define SYSCALL_SET_TID 0xDA
#define SYSCALL_EXIT 0x3C
#define SYSCALL_IOCTL 0x10
#define SYSCALL_WRITEV 0x14
#define SYSCALL_EXIT_GROUP 0xE7
#define SYSCALL_BRK 0x0C
#define SYSCALL_MMAP 0x9
#define SYSCALL_MPORTECT 0x0A
#define SYSCALL_FORK 0x39
#define SYSCALL_EXECVE 0x3B
#define SYSCALL_WAIT4 0x3D
#define SYSCALL_SIGPROCMASK 0x0E

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define PROT_GROWSDOWN 0x01000000
#define PROT_GROWSUP 0x02000000

void syscall_init(void);
int syscall_read(int fd, void* buf, long count);
int syscall_write(int fd, void* buf, long count);
int syscall_open(const char* path, int flags, int mode);
int syscall_arch_prctl(int code, unsigned long addr);
pid_t syscall_set_tid(uintptr_t tid);
int syscall_ioctl(int fd, uint32_t req, void* arg);
intptr_t syscall_brk(void* addr);
void* syscall_mmap(void* addr, size_t len, int prot, int flags, int fd,
                   long off);
int syscall_mprotect(void* addr, size_t len, int prot);
struct iovec {
	void* iov_base;
	int iov_len;
};

long syscall_writev(int fd, const struct iovec* iov, int iovcnt);
void syscall_exit_group(int status);
int syscall_fork(void);
int syscall_wait4(pid_t pid, int* wstatus, int options, void* rusage);
int64_t syscall_rt_sigprocmask(int how, void* set, void* oldset,
                               size_t sigsetsize);

int syscall_execve(const char* path, char* const argv[], char* const envp[],
                   interrupt_stack_frame_t* rsp);

#endif // __SYS__SYSCALL_H__
