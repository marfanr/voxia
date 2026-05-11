#include <str.h>
#include "vfs/vnode.h"
#include "init/init.h"
#include "libk/serial.h"
#include "llist.h"
#include "memory/slab.h"

static struct slab_cache* vnode_cache = 0;
static uint64_t vnode_id = 0;

INIT(Vnode) {
	LOG_INFO("vnode", "init");
	vxCreateSlabCache(&vnode_cache, "vnode", sizeof(vnode_t), 64, 0);
}

vnode_ptr_t KERNEL_API create_vnode() {
	auto vnode = (vnode_t*) vxSlabAlloc(vnode_cache);
	memset(vnode, 0, sizeof(*vnode));
	vnode->id = vnode_id++;
	return vnode;
}
void KERNEL_API vxFreeVnode(vnode_ptr_t vnode) {
	slab_free(vnode_cache, vnode);
}
