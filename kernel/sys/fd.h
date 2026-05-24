#ifndef __SYS_FD_H__
#define __SYS_FD_H__

#include <type.h>

#define INITIAL_MAX_FDS 64

struct fdtable;
struct file_descriptor {
    atomic_t count;
    struct fdtable *fdt;
    void* ops;
    uint8_t mode;
    uint64_t pos;
    uint32_t flags;
} __attribute__((aligned(64)));

struct fdtable {
    uint32_t max_fds;
    struct file_descriptor ** fds;
    uint32_t next_fd;
};

struct fdtable* alloc_fdtable();
void free_fdtable(struct fdtable* fdt);
struct file_descriptor* alloc_fd();

// TODO
int realloc_fdtable(struct fdtable* fdt, uint32_t max_fds);

#endif // __SYS_FD_H__