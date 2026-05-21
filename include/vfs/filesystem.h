#ifndef __VFS__FILESYSTEM_H__
#define __VFS__FILESYSTEM_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vnode vnode_t;
typedef struct dentry* dentry_ptr;
typedef struct cdev* cdev_ptr_t;

typedef struct filesystem filesystem_t;

struct fs_instance;
typedef struct fs_operations {
	int (*lookup)(struct fs_instance* instance, char* path, dentry_ptr parent, dentry_ptr *out);
} fs_operations_t;

struct fs_magic {
	const uint8_t magic[32];
	uint32_t count;
};

struct fs_data {
	struct fs_magic magic;
	uint32_t offset;
	fs_operations_t* ops;
};

struct filesystem {
	char name[16];
	struct fs_data data;
	struct filesystem* next;
} __attribute__((aligned(64)));
typedef filesystem_t* filesystem_ptr_t;

struct fs_instance {
	dentry_ptr block_dentry;
	cdev_ptr_t cdev;
	filesystem_ptr_t fs;
};

int create_filesystem( char name[16], struct fs_data* fs_data);
filesystem_ptr_t retrieve_filesystem(const char name[16]);
filesystem_t* get_all_filesystem();
filesystem_t* get_filesystem(const char name[16]);

#ifdef __cplusplus
}
#endif

#endif // __VFS__FILESYSTEM_H__