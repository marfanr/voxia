// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Mohammad Arfan

#ifndef __SYS__SYSCALL_H__
#define __SYS__SYSCALL_H__

#include "hal/cpu/interrupt.h"
#include "procc/process.h"
#include <str.h>
#include <type.h>

// unix/linux compatible
#define SYSCALL_READ 0X0
#define SYSCALL_WRITE 0X1
#define SYSCALL_OPEN 0X2
#define SYSCALL_CLOSE 0x3
#define SYSCALL_STAT 0X4
#define SYSCALL_FSTAT 0x5
#define SYSCALL_LSEEK 0x8
#define SYSCALL_FSYNC 74
#define SYSCALL_ARCH_PRCTL 158
#define SYSCALL_API 0X9F
#define SYSCALL_SET_TID 0xDA
#define SYSCALL_EXIT 0x3C
#define SYSCALL_IOCTL 0x10
#define SYSCALL_READV 0x13
#define SYSCALL_WRITEV 0x14
#define SYSCALL_EXIT_GROUP 0xE7
#define SYSCALL_BRK 0x0C
#define SYSCALL_MMAP 0x9
#define SYSCALL_MPORTECT 0x0A
#define SYSCALL_FORK 0x39
#define SYSCALL_EXECVE 0x3B
#define SYSCALL_WAIT4 0x3D
#define SYSCALL_RT_SIGACTION 0x0D
#define SYSCALL_SIGPROCMASK 0x0E
#define SYSCALL_PAUSE 0x22
#define SYSCALL_GETPID 0x27
#define SYSCALL_MOUNT 0xA5
#define SYSCALL_UNMOUNT 0xA6
#define SYSCALL_GETDENTS 0x4E
#define SYSCALL_GETDENTS64 0xD9
#define SYSCALL_NANOSLEEP 0x23
#define SYSCALL_CLONE 0x38
#define SYSCALL_FUTEX 0xCA
#define SYSCALL_SOCKET 0x29
#define SYSCALL_CONNECT 0x2A
#define SYSCALL_ACCEPT 0x2B
#define SYSCALL_SENDTO 0x2C
#define SYSCALL_RECVFROM 0x2D
#define SYSCALL_BIND 0x31
#define SYSCALL_LISTEN 0x32
#define SYSCALL_RT_SIGRETURN 0x0F
#define SYSCALL_SETSID 0x70
#define SYSCALL_GETCWD 79
#define SYSCALL_CHDIR 80
// mmap
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define PROT_GROWSDOWN 0x01000000
#define PROT_GROWSUP 0x02000000

void syscall_init(void);
int syscall_read(int fd, void* buf, long count);
int syscall_write(int fd, void* buf, long count);
int syscall_fsync(int fd);
int syscall_open(const char* path, int flags, int mode);
int syscall_arch_prctl(int code, unsigned long addr);
pid_t syscall_set_tid(uintptr_t tid);
int syscall_ioctl(int fd, uint32_t req, void* arg);
intptr_t syscall_brk(void* addr);
void* syscall_mmap(void* addr, size_t len, int prot, int flags, int fd,
                   long off);
int syscall_mprotect(void* addr, size_t len, int prot);
long syscall_dup(int oldfd);
long syscall_dup2(int oldfd, int newfd);
struct iovec {
	void* iov_base;
	int iov_len;
};

long syscall_readv(int fd, const struct iovec* iov, int iovcnt);
long syscall_writev(int fd, const struct iovec* iov, int iovcnt);
void syscall_exit_group(int status);
int syscall_fork(interrupt_stack_frame_t* rsp);
int syscall_wait4(pid_t pid, int* wstatus, int options, void* rusage);
int64_t syscall_rt_sigaction(int sig, const void* act, void* oact,
                             size_t sigsetsize);
int64_t syscall_rt_sigprocmask(int how, void* set, void* oldset,
                               size_t sigsetsize);
int syscall_execve(const char* path, char* const argv[], char* const envp[],
                   interrupt_stack_frame_t* rsp);

// following musl
struct stat {
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_nlink;
	uint32_t st_mode;
	uint32_t st_uid;
	uint32_t st_gid;
	uint32_t __pad0;
	uint64_t st_rdev;
	int64_t st_size;
	int64_t st_blksize;
	int64_t st_blocks;
	int64_t st_atime;
	int64_t st_atime_nsec;
	int64_t st_mtime;
	int64_t st_mtime_nsec;
	int64_t st_ctime;
	int64_t st_ctime_nsec;
	int64_t __unused[3];
};

int syscall_stat(const char* path, struct stat* buf);
int syscall_fstat(int fd, struct stat* buf);
int syscall_newfstatat(int dirfd, const char* pathname, struct stat* statbuf, int flags);
int syscall_close(int fd);

int syscall_mount(const char* source, const char* target, const char* fstype,
                  unsigned long flags, const void* data);
int syscall_unmount(const char* target, int flags);
int syscall_fstat(int fd, struct stat* statbuf);
int syscall_getdents64(int fd, void* buf, int size);
int syscall_getcwd(char* buf, size_t size);
int syscall_chdir(const char* path);
int syscall_fchdir(int fd);

struct timespec {
	long tv_sec;
	long tv_nsec;
};
int syscall_nanosleep(const struct timespec* req, struct timespec* rem);
long syscall_lseek(int fd, long offset, int whence);

// thread
int syscall_clone(interrupt_stack_frame_t* rsp);
int syscall_futex(int* uaddr, int futex_op, int val, const void* timeout, int* uaddr2, int val3);

// socket
int syscall_socket(int domain, int type, int protocol);
int syscall_bind(int fd, const void* addr, uint32_t len);
int syscall_listen(int fd, int backlog);
int syscall_connect(int fd, const void* addr, uint32_t len);
int syscall_accept(int fd, void* addr, uint32_t* addrlen);
int syscall_sendto(int fd, const void* buf, uint32_t len, int flags,
                   const void* dest_addr, uint32_t addrlen);
int syscall_recvfrom(int fd, void* buf, uint32_t len, int flags,
                     void* src_addr, uint32_t* addrlen);
long syscall_fcntl(int fd, int cmd, unsigned long arg);


// utils
void free_string_array(char** arr);
kstring safe_str_from_user(page_t pml4, const char* ustr);
char** copy_string_array(char* const* arr);
bool is_valid_user_pointer(page_t pml4, const void* ptr, size_t size);

#endif // __SYS__SYSCALL_H__
