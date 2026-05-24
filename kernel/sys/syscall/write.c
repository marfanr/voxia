#include "console/console.h"
#include "libk/serial.h"
#include "type.h"
#include <sys/syscall.h>

int syscall_write(int fd, void* buf, long count) {
    UNUSED(fd);
    UNUSED(buf);
    UNUSED(count);

    console_printf("%s\n", buf);
    
    return 0;
}