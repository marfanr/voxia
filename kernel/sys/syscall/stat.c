#include "libk/serial.h"
#include <sys/syscall.h>

int syscall_stat(const char* path, struct stat* buf) {
    serial2_printf("syscall_stat: path=%s, buf=%p\n", path, buf);
    return -1;
}