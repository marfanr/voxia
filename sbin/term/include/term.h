#ifndef __TERM_H__
#define __TERM_H__

int read(int fd, void* buf, long count);
int write(int fd, void* buf, long count);
int open(const char* path, int flags, int mode);

#endif //