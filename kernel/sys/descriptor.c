#include "descriptor.h"
#include "init/init.h"
#include "vfs/dentry.h"

#include <libk/serial.h>
#include <libk/str.h>
#include <libk/vector.h>
#include <memory/slab.h>

define_vector(file_descriptor_ptr);
static vector(file_descriptor_ptr) descriptors = {0};
static struct slab_cache *descriptor_cache     = 0;

#define MAX_FD_NUMBER 512

INIT(descriptor)
{
    vector_init(&descriptors);
    serial_trace("descriptor installed at %x\n", (uintptr_t)&descriptors);
    slab_cache_create(&descriptor_cache, "descriptor", sizeof(file_descriptor_t), 64, 0);
}

int
descriptor_add(vfs_inode_t *inode, dentry_ptr dentry, uint8_t flags)
{
    file_descriptor_t *fd = (file_descriptor_t *)slab_alloc(descriptor_cache);
    fd->inode             = inode;
    fd->dentry            = dentry;
    fd->flags             = flags;
    fd->offset            = inode->offset;
    fd->addr              = inode->block->bar;
    vector_push_back(&descriptors, fd);
    return descriptors.size - 1;
}

file_descriptor_t *
descriptor_get(int id)
{
    // need o check maybe causing PF
    file_descriptor_t *fd = (file_descriptor_t *)descriptors.data[id];
    return fd;
}

void
descriptor_free(int fd)
{
}