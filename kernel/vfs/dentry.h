#ifndef __VFS__DENTRY_H__
#define __VFS__DENTRY_H__

#include <libk/string.h>
#include <libk/type.h>
#include <libk/vector.h>

struct vfs_inode;

typedef struct dentry  dentry_t;
typedef struct dentry *dentry_ptr;
define_vector(dentry_ptr);

struct dentry
{
    string            name;
    struct vfs_inode *inode;
    dentry_ptr        parent;
    vector(dentry_ptr) children;
    uintptr_t addr;
} __attribute__((aligned(32)));

dentry_ptr create_directory_entry(string, struct vfs_inode *inode, dentry_t *parent);

#endif // __VFS__DENTRY_H__