#ifndef __VFS__FILESYSTEM_H__
#define __VFS__FILESYSTEM_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

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
	void* private_data;
	struct filesystem* next;
};
typedef filesystem_t* filesystem_ptr_t;

int vxCreateFilesystem(const char name[16], fs_operations_t* ops,
		       void* private_data);
filesystem_ptr_t vxFindFilesystem(const char name[16]);
filesystem_t* get_all_filesystem();
filesystem_t* get_filesystem(const char name[16]);

#ifdef __cplusplus
}
#endif

#endif // __VFS__FILESYSTEM_H__