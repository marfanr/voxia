#ifndef __SYS__SYSCALL_H__
#define __SYS__SYSCALL_H__

#include <libk/type.h>

#define SYSCALL_WRITE 0X1
#define SYSCALL_READ 0X2
#define SYSCALL_OPEN 0X3
#define SYSCALL_FSTAT 0X4
#define SYSCALL_ALLOC 0x8
#define SYSCALL_EXIT 0x9
#define SYSCALL_API 0X9F

#define SYS_API_GRAPHIC 0XAA73CCFF

uint64_t  sys_write(uint64_t descriptor, const char *buffer, uint64_t length);
int       sys_read(int descriptor, char *buffer, uint64_t length);
uintptr_t sys_alloc(uint64_t size);
uint64_t  sys_api(uint64_t identifier, int version);
uint64_t  sys_open(const char *path, uint64_t flags);
void      sys_exit(int exit_code);
int       fstat(int fd, uint8_t *buf);
#endif // __SYS__SYSCALL_H__
