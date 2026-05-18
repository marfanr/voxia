#include "console/console.h"
#include "init/init.h"
#include "ioforge/ioforge_block.h"
#include "libk/debug/debug.h"
#include "libk/fs/iso9660.h"
#include "libk/serial.h"
#include <string.h>
#include <vector.h>
#include "memory/slab.h"
#include "notify.h"
#include "str.h"
#include "vfs/vnode.h"
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
static void vfs_event_handler(uint32_t event, void* data, void* ctx);

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

	// create notify
	notify_dev_create(str("/vfs/block"));
	{
		auto n = (struct notifier*) kalloc(sizeof(struct notifier));
		memset(n, 0, sizeof(struct notifier));
		n->callback = vfs_event_handler;
		n->context = 0;
		n->priority = NOTIFY_HIGHT;
		n->flags = 0;

		notify_register(str("/vfs/block"), n);
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
		serial2_printf("dentry not found..");
		return VFS_DEV_NOT_FOUND;
	}

	UNUSED(fs);
	UNUSED(flags);

	if (!dev_entry) {
		LOG_WARN("VFS", "vfs_mount: dev %s not found", dev);
		return VFS_ENOENT;
	}

	auto dev_vnode = dev_entry->vnode;
	if (dev_vnode) {
		LOG_DEBUG("VFS", "cdev minor %d major %d",
			  dev_vnode->device.dev.minor,
			  dev_vnode->device.dev.major);
		// auto cdev = vxRetrieveDev(dev_vnode->major ? dev_vnode->major : 0,
		//                           dev_vnode->minor ? dev_vnode->minor : 0);
	}

	return VFS_OK;
}

KERNEL_API int
vfs_mount_dev(vnode_ptr_t dev_vnode, char* fs, dentry_ptr dentry, int flags) {
	if (!fs || !dentry || dev_vnode)
		return VFS_ERR;

	UNUSED(fs);
	UNUSED(flags);

	if (dev_vnode->type != VNODE_TYPE_DEV)
		LOG_WARN("VFS", "vfs_mount: dev not a device");
	return VFS_ERR;

	if (dev_vnode) {
		LOG_DEBUG("VFS", "vfs_mount_dev: cdev minor %d major %d",
			  dev_vnode->device.dev.minor,
			  dev_vnode->device.dev.major);
		// auto cdev = vxRetrieveDev(dev_vnode->major ? dev_vnode->major : 0,
		//                           dev_vnode->minor ? dev_vnode->minor : 0);
	}

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

static void detect_cd_filesystem(vnode_ptr_t vnode, void* data, void* ctx) {
	UNUSED(data);
	UNUSED(ctx);

	auto ops = (vops_blk_t*) vnode->ops;
	if (!ops || !ops->read)
		return;

	auto request_size = sizeof(struct iso9660_pvd);
	uint8_t* d_ = (uint8_t*) kalloc(request_size);
	if (!d_)
		return;

	memset(d_, 0, request_size);

	auto cdev =
		retrieve_dev(vnode->device.dev.major, vnode->device.dev.minor);

	if (!cdev) {
		serial2_printf("cdev not found\n");
		kfree2(d_);
		return;
	}

	// ISO9660 PVD is always at byte 32768 (sector 16 of 2048 bytes)
	int ret = ops->read(ops->v_data, 32768, d_, request_size);

	if (ret < 0) {
		serial2_printf("read failed: %d\n", ret);
		kfree2(d_);
		return;
	}

	struct iso9660_pvd* pvd = (struct iso9660_pvd*) (void*) d_;
	if (strncmp(pvd->id, "CD001", 5) == 0) {
		LOG2_INFO("VFS NOTIFY", "terdeteksi ISO9660 CD-ROM pada %s",
			  cdev->name);

		// 	// trying to mount
		// 	// {
		// 	// 	dentry_ptr mount_entry;
		// 	// 	vxnamei("/tmp/root", &mount_entry);
		// 	// 	vfs_mount_dev(vnode, "ISO9660", mount_entry, 0);

		// 	// 	// TODO: check is a signature file indicated boot artition is existed
	}
	kfree2(d_);
}

static void vfs_notify_probe_handler(void* data, void* ctx) {

	UNUSED(ctx);

	if (!data)
		return;

	vnode_ptr_t vnode = (vnode_ptr_t) data;

	auto ops = (vops_blk_t*) vnode->ops;

	if (ops == 0)
		return;

	auto dev_major = vnode->device.dev.major;

	serial2_printf("vnode major %d %x vdata %x\n", dev_major, ops,
		       ops->v_data);

	if (dev_major == MAJOR_CDROM) {
		detect_cd_filesystem(vnode, data, ctx);
	}
}

static void vfs_event_handler(uint32_t event, void* data, void* ctx) {
	UNUSED(data);
	UNUSED(ctx);

	switch (event) {
	case VFS_NOTIFY_PROBE:
		vfs_notify_probe_handler(data, ctx);
		break;

	default:
		break;
	}

	LOG2_DEBUG("VFS NOTIFY", "new event detected (%d)", event);
	KDEBUG(DEBUG_LEVEL_OK, "vfs notify received\n");
}

#undef RBT_ID_NAME
#undef RBT_TYPE
