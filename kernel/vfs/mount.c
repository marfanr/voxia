#include "vfs/mount.h"
#include "init/init.h"
#include "memory/slab.h"

static struct slab_cache* mount_cache;

typedef struct mount_cache_node {
	uint8_t* obj[64];
} mount_cache_node_t;

static mount_cache_node_t* mount_cache_node;

INIT(Mount) {
	vxCreateSlabCache(&mount_cache, "mount", sizeof(mount_t), 64, 0);
}

mount_ptr_t KERNEL_API vxAllocMountTable() {
	return (mount_ptr_t) vxSlabAlloc(mount_cache);
}
