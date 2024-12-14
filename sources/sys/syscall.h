#ifndef __SYS__SYSCALL_H__
#define __SYS__SYSCALL_H__

#include <libk/type.h>

#define SYSCALL_WRITE 0X1
#define SYSCALL_READ 0X2
#define SYSCALL_OPEN 0X3

#define SYS_API_GRAPHIC 0XAA73CCFF

void sys_write(uint64_t descriptor, const char *buffer, uint64_t length);

#endif // __SYS__SYSCALL_H__
