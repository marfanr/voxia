#ifndef __SYS__DESCRIPTOR_H__
#define __SYS__DESCRIPTOR_H__

#include "vfs/dentry.h"
#include <libk/type.h>
#include <vfs/vfs.h>

#define FD_FLAG_READ 1
#define FD_FLAG_WRITE 1 << 1

typedef struct file_descriptor
{
    vfs_inode_t *inode;
    dentry_ptr   dentry;
    uintptr_t    addr;
    uint64_t     offset;
    uint8_t      flags;
    uint64_t     page;
} __attribute__((aligned(32))) file_descriptor_t;

typedef file_descriptor_t *file_descriptor_ptr;

int                descriptor_add(vfs_inode_t *inode, dentry_ptr dentry, uint8_t flags);
file_descriptor_t *descriptor_get(int fd);
void               descriptor_free(int fd);

#endif // __SYS__DESCRIPTOR_H__
