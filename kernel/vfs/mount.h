#ifndef __VFS__MOUNT_H__
#define __VFS__MOUNT_H__

#include "vfs/dentry.h"
#include "vfs/dev.h"
#include "vfs/vnode.h"

typedef struct mount {
	cdev_ptr_t dev;
	dentry_ptr mount_point;
	vnode_ptr_t root;
	int flags;
} mount_t;
typedef mount_t* mount_ptr_t;

mount_ptr_t vxAllocMountTable();

#endif // __VFS__MOUNT_H__