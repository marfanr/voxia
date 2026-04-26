#ifndef __VFS__FILESYSTEM_H__
#define __VFS__FILESYSTEM_H__

#include <libk/type.h>

typedef struct vnode vnode_t;
typedef struct dentry* dentry_ptr;

typedef struct filesystem filesystem_t;
typedef struct fs_operations {
	int (*mount)(filesystem_t* fs, void* dev, vnode_t** root);
	int (*unmount)(filesystem_t* fs);
	int (*sync)(filesystem_t* fs);
} fs_operations_t;

struct filesystem {
	const char name[16];
	fs_operations_t* ops;
	struct filesystem* next;
};
typedef filesystem_t* filesystem_ptr_t;

int vxCreateFilesystem(const char name[16], filesystem_ptr_t fs);
filesystem_ptr_t vxFindFilesystem(const char name[16]);

#endif // __VFS__FILESYSTEM_H__