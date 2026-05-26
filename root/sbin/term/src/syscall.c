#include "term.h"

__inline unsigned long __syscall3(long n, long a1, long a2, long a3) {
	unsigned long ret;
	__asm__ __volatile__("syscall"
	                     : "=a"(ret)
	                     : "a"(n), "D"(a1), "S"(a2), "d"(a3)
	                     : "rcx", "r11", "memory");
	return ret;
}

int read(int fd, void* buf, long count) {
	return (int)__syscall3(0, fd, (long)buf, (long)count);
}

int write(int fd, void* buf, long count) {
	return (int)__syscall3(1, fd, (long)buf, (long)count);
}

int open(const char* path, int flags, int mode) {
	return (int)__syscall3(2, (long)path, (long)flags, (long)mode);
}