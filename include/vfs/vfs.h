
#ifndef __VFS__VFS_H__
#define __VFS__VFS_H__

#include "vfs/dentry.h"
#include <type.h>
#include <vector.h>
#include <vfs/vnode.h>

#define ROOT_UUID 0

enum {
	VFS_NOTIFY_PROBE = 1,
};

#ifdef __cplusplus
extern "C" {
#endif

vnode_t* create_and_attach_vnode();

int vfs_mount_dev(vnode_ptr_t dev_vnode, char* fs, dentry_ptr dentry,
                  int flags);
int vfs_umount(dentry_ptr dentry);

#ifdef __cplusplus
}
#endif

#endif // __VFS__VFS_H__
