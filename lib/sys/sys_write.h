#ifndef __SYS__SYS_WRITE_H__
#define __SYS__SYS_WRITE_H__

#include <libk/type.h>

int sys_write(int fd, const void *buf, uint16_t count);
int sys_write(int fd, const void *buf, uint16_t count, uint8_t flag);

#endif // __SYS__SYS_WRITE_H__
