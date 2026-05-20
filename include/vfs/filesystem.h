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

struct fs_magic {
	uint32_t magic[32];
	uint32_t count;
};

struct fs_data {
	struct fs_magic magic;
	uint32_t offset;
	fs_operations_t* ops;
	void* private_data;
};

struct filesystem {
	char name[16];
	struct fs_data data;
	struct filesystem* next;
} __attribute__((aligned(64)));
typedef filesystem_t* filesystem_ptr_t;

int create_filesystem( char name[16], struct fs_data* fs_data);
filesystem_ptr_t retrieve_filesystem(const char name[16]);
filesystem_t* get_all_filesystem();
filesystem_t* get_filesystem(const char name[16]);

#ifdef __cplusplus
}
#endif

#endif // __VFS__FILESYSTEM_H__