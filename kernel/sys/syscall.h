#ifndef __SYS__SYSCALL_H__
#define __SYS__SYSCALL_H__

#include <type.h>

#define SYSCALL_READ 0X0
#define SYSCALL_WRITE 0X1
#define SYSCALL_OPEN 0X2
#define SYSCALL_FSTAT 0X4
#define SYSCALL_ALLOC 0x8
#define SYSCALL_EXIT 0x9
#define SYSCALL_API 0X9F

void syscall_init(void);
int syscall_read(int fd, void* buf, long count);
int syscall_write(int fd, void* buf, long count);

#endif // __SYS__SYSCALL_H__
