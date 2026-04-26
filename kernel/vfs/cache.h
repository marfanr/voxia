#ifndef __VFS__CACHE_H_
#define __VFS__CACHE_H_

#include "libk/string.h"
#include "vfs/vnode.h"

typedef struct vfs_cache vfs_cache_t;
struct vfs_cache {
	string path;
	vnode_ptr_t inode;
	struct vfs_cache* next;
} __attribute__((aligned(64)));

typedef struct {
	vfs_cache_t* head;
	uint64_t last_access;
} vfs_cache_blob_t;

void vxCreateNodeCache(string path, vnode_ptr_t inode);
vnode_ptr_t vxLookupNodeCache(string path);

#endif // __VFS__CACHE_H_