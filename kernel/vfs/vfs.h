
#ifndef __VFS__VFS_H__
#define __VFS__VFS_H__

#include "vfs/dentry.h"
#include "vfs/vnode.h"
#include <libk/type.h>
#include <libk/vector.h>

#define ROOT_UUID 0

vnode_t* vxCreateAndAttachVnode();

int vxMakeDirectory(dentry_ptr dir, dentry_ptr dentry, uint16_t permission);
int vxVFSMount(char* dev, char* path, char* fs, int flags);
#endif // __VFS__VFS_H__
