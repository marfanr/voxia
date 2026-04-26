#include "vfs/vnode.h"
#include "init/init.h"
#include "libk/vector.h"
#include "memory/slab.h"

static struct slab_cache* vnode_cache = 0;
static uint64_t vnode_id = 0;

INIT(Vnode) {
	vxCreateSlabCache(&vnode_cache, "vnode", sizeof(vnode_t), 64, 0);
}

vnode_ptr_t vxAllocVnode() {
	auto vnode = (vnode_t*)vxSlabAlloc(vnode_cache);
	vnode->id = vnode_id++;
	vector_init(&vnode->dentry_list);
	return vnode;
}
void vxFreeVnode(vnode_ptr_t vnode) { slab_free(vnode_cache, vnode); }
