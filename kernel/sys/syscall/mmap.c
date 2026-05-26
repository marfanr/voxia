#include "libk/serial.h"
#include "type.h"
#include <sys/syscall.h>

void* syscall_mmap(void* addr, size_t len, int prot, int flags, int fd,
                   long off) {
	(void)addr;
	(void)len;
	(void)prot;
	(void)flags;
	(void)fd;
	(void)off;

	serial_trace("mmap_request: addr 0x%x, len 0x%x, prot 0x%x, flags "
	             "0x%x, fd 0x%x, off 0x%x\n",
	             addr, len, prot, flags, fd, off);

	return 0;
}