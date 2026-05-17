#include "init/init.h"
#include "libk/serial.h"
#include <string.h>
#include <vector.h>
#include "memory/slab.h"
#include <vfs/dentry.h>
#include <vfs/dev.h>
#include <vfs/enum.h>
#include <vfs/filesystem.h>
#include <vfs/mount.h>
#include <vfs/vnode.h>
#include <type.h>
#include <vfs/vfs.h>

#define RBT_TYPE vnode_t
#define RBT_ID_NAME id
#include <libk/tree/rbt.h>

// definition
#define VFS_DEBUG 1
#define PATH_MAX 4096
#define NAME_MAX 255
#define VFS_MAX_FS 512
#define VFS_MAX_PATH_CACHE 512
#define CALC_PATH_HASH_LEN(path, curr_depth)                                   \
	4096 + 1024 * pow(strlen(path), curr_depth)

// internal use
static rbt_node* NIL;
static rbt_node* vfs_tree;
static struct slab_cache* rbt_node_cache;

KERNEL_API vnode_t* create_and_attach_vnode() {
	vnode_t* vnode = create_vnode();

	rbt_node* node = (rbt_node*) vxSlabAlloc(rbt_node_cache);
	memset(node, 0, sizeof(rbt_node)); // ← tambah ini
	rbt_insert_node(&vfs_tree, node, vnode, NIL);

	return vnode;
}

INIT(Vfs) {
	vxCreateSlabCache(&rbt_node_cache, "rbt_node", sizeof(rbt_node), 64, 0);

	NIL = (struct rbt_node*) vxSlabAlloc(rbt_node_cache);
	memset(NIL, 0, sizeof(rbt_node)); // ← tambah ini
	NIL->data = create_vnode();
	NIL->left = NIL->right = NIL->parent = NIL;
	vfs_tree = NIL;

	// create root inode
	auto root_inode = create_and_attach_vnode();
	root_inode->permission = 660;
	root_inode->type = VNODE_TYPE_DIR;

	// create root dentry
	{
		auto entry = create_dentry(str("/"), root_inode, 0);
		vxSetDentryAsRoot(entry);
	}

	LOG_INFO("vfs", "vfs has been installed");
}

KERNEL_API int
vxMakeDirectory(dentry_ptr dir, dentry_ptr dentry, uint16_t permission) {
	UNUSED(dir);
	// vxAttachDentryToParent(dentry, dir);
	dentry->vnode->permission = permission;
	dentry->vnode->type = VNODE_TYPE_DIR;
	return VFS_OK;
}

KERNEL_API int vfs_mount(char* dev, char* fs, dentry_ptr dentry, int flags) {
	if (!fs || !dentry)
		return VFS_ERR;

	dentry_ptr dev_entry;
	if (vxResolveDentry(dev, 0, &dev_entry, 0) == VFS_ENOENT) {
		LOG_WARN("VFS", "dentry not found..");
		return VFS_DEV_NOT_FOUND;
	}

	UNUSED(fs);
	UNUSED(flags);

	// auto dev_vnode = dev_entry->vnode;
	// auto cdev = vxRetrieveDev(dev_vnode->major ? dev_vnode->major : 0,
	//                           dev_vnode->minor ? dev_vnode->minor : 0);

	// LOG_DEBUG("VFS", "cdev minor %d major %d", cdev->minor, cdev->major);

	// // if (dev_vnode->type != VNODE_TYPE_BLK) {
	// // 	return VFS_NOT_A_BLOCK_DEVICE;
	// // }

	// // LOG_INFO("VFS", "dentry found name  %s", dev_entry->name->c_str);

	// auto fs_ = vxFindFilesystem(fs);
	// if (!fs_) {
	// 	return VFS_FS_NOT_FOUND;
	// }

	// // resolve path
	// {
	// 	dentry_ptr path_entry;
	// 	if (vxResolveDentry(path, 0, &path_entry, 0) == VFS_ENOENT) {
	// 		return VFS_ENOENT;
	// 	}
	// 	auto mount = vxAllocMountTable();
	// 	auto path_vnode = (vnode_ptr_t)path_entry->vnode;
	// 	path_vnode->mounted = mount;
	// 	mount->mount_point = path_entry;
	// 	mount->root = (vnode_ptr_t)dev_vnode;
	// 	mount->dev = cdev;
	// 	mount->flags = flags;

	// 	// LOG_INFO("VFS", "dentry found name  %s",
	// 	//          path_entry->name->c_str);
	// }

	// run filesystem init function if exits
	// if (fs_->ops->mount)
	// 	fs_->ops->mount((vnode_ptr_t)dev_vnode, dev_entry);

	return VFS_OK;
}

int KERNEL_API vxVFSOpen(char* path, int flags) {
	UNUSED(flags);
	// will be implemented
	dentry_ptr dentry;
	vxResolveDentry(path, 0, &dentry, 0);
	LOG_DEBUG("VFS", "opened %s", dentry->name->c_str);
	return VFS_OK;
}

#undef RBT_ID_NAME
#undef RBT_TYPE
