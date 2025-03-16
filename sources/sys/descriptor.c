#include "descriptor.h"

#include "memory/buddy_allocator.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include <libk/debug/debug.h>
#include <libk/serial.h>
#include <memory/memory_utils.h>

struct file_descriptor **g__descriptor_ = 0;
struct buddy_allocator *descriptor_allocator = 0;

#define MAX_FD_NUMBER 512

void
descriptor_install ()
{
    // TO DO: iplemnt vec tor for dynamic size
    size_t estimated_size = 1024 * 1024 * 4;
    descriptor_allocator = buddy_allocator_install (
        (void *)VIRT2PHYS (phys_base_alloc (1 + (estimated_size / 4096))),
        (1 + (estimated_size / 4096)) * 4096);
    g__descriptor_ = (struct file_descriptor *)buddy_alloc (
        descriptor_allocator, MAX_FD_NUMBER * sizeof (uintptr_t));
    serial_trace ("descriptor installed : 0x%x\n", g__descriptor_);
}

int
descriptor_add (int inode, uintptr_t addr, uint64_t offset, uint8_t flags)
{
    for (int i = 0; i < MAX_FD_NUMBER; i++)
        {
            if (g__descriptor_[i] == 0)
                {
                    struct file_descriptor *fd
                        = (struct file_descriptor *)buddy_alloc (
                            descriptor_allocator,
                            sizeof (struct file_descriptor));
                    serial_trace ("descriptor added : %x\n", fd);
                    fd->addr = addr;
                    fd->flags = flags;
                    fd->inode = inode;
                    fd->offset = offset;
                    g__descriptor_[i] = fd;
                    return i;
                }
        }
}

struct file_descripor *
descriptor_get (int fd)
{
    return g__descriptor_[fd];
}

void
descriptor_free (int fd)
{
    if (g__descriptor_[fd] != 0)
        {
            phys_base_free ((uint64_t)g__descriptor_[fd], 1);
            g__descriptor_[fd] = 0;
        }
}