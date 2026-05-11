
#ifndef __VFS__VFS_H__
#define __VFS__VFS_H__

#include "vfs/dentry.h"
#include <vfs/vnode.h>
#include <type.h>
#include <vector.h>

#define ROOT_UUID 0

#ifdef __cplusplus
extern "C" {
#endif

vnode_t* create_and_attach_vnode();

int vxMakeDirectory(dentry_ptr dir, dentry_ptr dentry, uint16_t permission);
int vxVFSMount(char* dev, char* path, char* fs, int flags);

#ifdef __cplusplus
}
#endif

#endif // __VFS__VFS_H__
