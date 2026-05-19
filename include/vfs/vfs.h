
#ifndef __VFS__VFS_H__
#define __VFS__VFS_H__

#include "vfs/dentry.h"
#include <vfs/vnode.h>
#include <type.h>
#include <vector.h>

#define ROOT_UUID 0

enum {
    VFS_NOTIFY_PROBE = 1,
};

#ifdef __cplusplus
extern "C" {
#endif

vnode_t* create_and_attach_vnode();

int vxMakeDirectory(dentry_ptr dir, dentry_ptr dentry, uint16_t permission);
int vxVFSMount(char* dev, char* fs_type, dentry_ptr mount_dentry, int flags);

#ifdef __cplusplus
}
#endif

#endif // __VFS__VFS_H__
