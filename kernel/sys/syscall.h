#ifndef __SYS__SYSCALL_H__
#define __SYS__SYSCALL_H__

#include "procc/process.h"
#include <type.h>


// unix/linux compatible
#define SYSCALL_READ 0X0
#define SYSCALL_WRITE 0X1
#define SYSCALL_OPEN 0X2
#define SYSCALL_FSTAT 0X4
#define SYSCALL_ALLOC 0x8
#define SYSCALL_ARCH_PRCTL 158
#define SYSCALL_API 0X9F
#define SYSCALL_SET_TID 0xDA
#define SYSCALL_EXIT 0x3C
#define SYSCALL_IOCTL 0x10
#define SYSCALL_WRITEV 0x14

void syscall_init(void);
int syscall_read(int fd, void* buf, long count);
int syscall_write(int fd, void* buf, long count);
int syscall_open(const char* path, int flags, int mode);
int syscall_arch_prctl(int code, unsigned long addr);
pid_t syscall_set_tid(uint32_t tid);
int ioctl(int fd, uint32_t req, void* arg);

#endif // __SYS__SYSCALL_H__
