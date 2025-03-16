#ifndef __SYS__DESCRIPTOR_H__
#define __SYS__DESCRIPTOR_H__

#include <libk/type.h>

#define FD_FLAG_READ 1
#define FD_FLAG_WRITE 1 << 1

struct file_descriptor
{
    int inode;
    uintptr_t addr;
    uint64_t offset;
    uint8_t flags;
    uint64_t page;
};

int descriptor_add (int inode, uintptr_t addr, uint64_t offset, uint8_t flags);
struct file_descripor *descriptor_get (int fd);
void descriptor_free (int fd);
void descriptor_install ();

#endif // __SYS__DESCRIPTOR_H__
