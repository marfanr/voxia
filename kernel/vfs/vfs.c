#include "console/console.h"
#include "init/init.h"
#include "ioforge/ioforge_block.h"
#include "libk/debug/debug.h"
#include "libk/fs/fat32.h"
#include "libk/fs/iso9660.h"
#include "libk/serial.h"
#include "llist.h"
#include "memory/slab.h"
#include "notify.h"
#include "str.h"
#include "vfs/vnode.h"
#include <string.h>
#include <type.h>
#include <vector.h>
#include <vfs/dentry.h>
#include <vfs/dev.h>
#include <vfs/enum.h>
#include <vfs/filesystem.h>
#include <vfs/mount.h>
#include <vfs/vfs.h>
#include <vfs/vnode.h>

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

	rbt_node* node = (rbt_node*)vxSlabAlloc(rbt_node_cache);
	rbt_insert_node(&vfs_tree, node, vnode, NIL);

	return vnode;
}

INIT(Vfs) {
	vxCreateSlabCache(&rbt_node_cache, "rbt_node", sizeof(rbt_node), 64, 0);

	NIL = (struct rbt_node*)vxSlabAlloc(rbt_node_cache);
	memset(NIL, 0, sizeof(rbt_node));
	NIL->color = RBT_BLACK;
	NIL->left = NIL->right = NIL->parent = NIL;
	vfs_tree = NIL;

	// create root inode
	auto root_inode = create_and_attach_vnode();
	root_inode->permission = 660;
	root_inode->type = VNODE_TYPE_DIR;

	// create root dentry
	{
		auto entry = create_dentry(str("/"), root_inode, 0);
		dentry_get(entry);
		entry->flags |= DENTRY_PINNED;
		vxSetDentryAsRoot(entry);
	}

	/* registering filesystem */
	{
		auto iso_fs = (struct fs_data){
		    .magic =
		        {
		            .magic = {'C', 'D', '0', '0', '1'},
		            .count = 5,
		        },
		    .ops = iso9660_fs_operations(),
			.file_ops = iso9660_file_operations(),
		};
		create_filesystem("ISO9660", &iso_fs);
	}

	{
		auto fat32_fs = (struct fs_data){
		    .magic =
		        {
		            .magic = {'F', 'A', 'T', '3', '2', ' ', ' ', ' '},
		            .count = 8,
		        },
		    .ops = fat32_fs_operations(),
			.file_ops = fat32_file_operations(),
		};
		create_filesystem("FAT32", &fat32_fs);
	}

	// create notify
	notify_dev_create(str("/vfs/block"));
	notify_dev_create(str("/vfs/root"));

	// handle block notify
	{
		auto n = (struct notifier*)kalloc(sizeof(struct notifier));
		memset(n, 0, sizeof(struct notifier));
		n->callback = vfs_event_handler;
		n->context = 0;
		n->priority = NOTIFY_HIGHT;
		n->flags = 0;

		notify_register("/vfs/block", n);
	}

	LOG_INFO("vfs", "vfs has been installed");
}

KERNEL_API int vfs_mount(dentry_ptr dev_dentry, char* fs, dentry_ptr dentry,
                         int flags) {
	if (!fs || !dev_dentry || !dentry)
		return VFS_ERR;

	UNUSED(flags);

	auto fs_ = retrieve_filesystem(fs);
	if (!fs_) {
		LOG2_WARN("VFS", "vfs_mount: fs %s not found", fs);
		return VFS_ERR;
	}

	serial2_printf("[DEBUG] vfs_mount: fs %s found at %x, ops is %x\n", fs,
	               fs_, fs_->data.ops);

	if (!fs_->data.ops) {
		LOG2_WARN("VFS", "vfs_mount: fs %s ops not found", fs);
		serial2_printf("vfs fs magic count %d\n",
		               fs_->data.magic.count);
		serial2_printf("vfs fs ops 0x%x\n", fs_->data.ops);
		return VFS_ERR;
	}

	auto dev_vnode = dev_dentry->vnode;
	if (!dev_vnode) {
		LOG2_WARN("VFS", "vfs_mount_dev: dev vnode not found");
		return VFS_ERR;
	}

	if (dev_vnode->type != VNODE_TYPE_BLK) {
		LOG2_WARN("VFS", "vfs_mount: dev not a block device");
		return VFS_ERR;
	}

	auto cdev =
	    retrieve_dev(dev_vnode->device.major, dev_vnode->device.minor);

	if (!cdev) {
		LOG2_WARN("VFS", "vfs_mount_dev: cdev not found");
		return VFS_ERR;
	}
	LOG_DEBUG("VFS", "vfs_mount_dev: cdev minor %d major %d", cdev->minor,
	          cdev->major);

	vnode_ptr_t dentry_node = dentry->vnode;
	if (!dentry_node) {
		LOG2_INFO("VFS",
		          "vfs_mount: created vnode for mount point dentry");
		dentry_node = create_vnode();
		dentry->vnode = dentry_node;
	}

	dentry_node->type = VNODE_TYPE_DIR;
	dentry_node->mountedhere = cdev;
	dev_vnode->mount = cdev;

	struct fs_instance* fs_ins =
	    (struct fs_instance*)kalloc(sizeof(struct fs_instance));
	fs_ins->fs = fs_;
	fs_ins->block_dentry = dev_dentry;
	fs_ins->cdev = cdev;

	dentry_node->fs_instance = fs_ins;
	dentry_node->ops = fs_ins->fs->data.file_ops;

	/* Pin mount point agar tidak dievict selama masih ada FS di-mount */
	dentry->flags |= DENTRY_MOUNTPOINT | DENTRY_PINNED;

	fs_->data.ops->lookup(fs_ins, 0, 0, &dentry);

	if (fs_->data.ops->mount) {
		fs_->data.ops->mount(fs_ins);
	}

	dentry_get(dev_dentry);

	LOG2_DEBUG("VFS", "mounted %d:%d on %s with filesystem %s\n",
	           cdev->major, cdev->minor, dentry->name->c_str, fs_->name);
	return VFS_OK;
}

static int vfs_umount_recursive(dentry_t* dentry) {
	if (!dentry)
		return VFS_OK;

	auto ch = dentry->child_list.next;
	while (ch != &dentry->child_list) {
		auto next = ch->next;
		dentry_t* child = container_of(ch, dentry_t, siblings);

		int ret = vfs_umount_recursive(child);
		if (ret != VFS_OK)
			return ret;

		ch = next;
	}

	if (get_reffcount(dentry) > 1) {
		LOG2_WARN("VFS", "umount: dentry '%s' (%d) still in use",
		          dentry->name ? dentry->name->c_str : "?",
		          dentry->refcount.counter);
		return VFS_ERR_BUSY;
	}

	if (dentry->flags & DENTRY_IN_CACHE) {
		dentry->flags &= ~(uint32_t)DENTRY_PINNED;
		cache_remove(get_root_cache(), dentry);
	}
	return VFS_OK;
}

int vfs_umount(dentry_ptr dentry) {
	if (!dentry)
		return VFS_ERR;

	auto vnode = dentry->vnode;
	if (!vnode)
		return VFS_ERR;

	if (vnode->type == VNODE_TYPE_DIR) {
		LOG2_DEBUG("Umount", "%s is directory", dentry->name->c_str);
		auto cdev = vnode->mountedhere;
		if (!cdev) {
			LOG2_WARN("Unmount", "no mounted here");
			return VFS_ERR;
		}
		LOG2_DEBUG("Umount", "mounted here %d:%d", cdev->major,
		           cdev->minor);
		auto fs_ins = vnode->fs_instance;
		if (fs_ins && fs_ins->fs && fs_ins->fs->data.ops->umount) {
			fs_ins->fs->data.ops->umount(fs_ins);
		}
		dentry_put(vnode->fs_instance->block_dentry);
		dentry->vnode->mountedhere = 0;

	} else if (vnode->type == VNODE_TYPE_BLK) {
		LOG2_DEBUG("Umount", "%s is block", dentry->name->c_str);
		vnode->mount = 0;

	} else {
		LOG2_WARN("Umount", "%s is unknown", dentry->name->c_str);
		return VFS_ERR;
	}

	KDEBUG(DEBUG_LEVEL_OK, "mounted %s\n", dentry->name->c_str);

	/* Unpin mount point sebelum put agar auto-evict bisa berjalan */
	dentry->flags &= ~(uint32_t)(DENTRY_PINNED | DENTRY_MOUNTPOINT);
	dentry_put(dentry);
	auto ok = vfs_umount_recursive(dentry);
	if (ok != VFS_OK)
		dentry_get(dentry);
	return ok;
}

__attribute__((unused)) static void
detect_cd_filesystem(dentry_ptr dentry, void* data, void* ctx) {
	UNUSED(data);
	UNUSED(ctx);

	auto vnode = dentry->vnode;
	if (!vnode)
		return;

	auto ops = (vops_blk_t*)vnode->ops;
	if (!ops || !ops->read)
		return;

	auto request_size = sizeof(struct iso9660_pvd);
	uint8_t* d_ = (uint8_t*)kalloc(request_size);
	if (!d_)
		return;

	memset(d_, 0, request_size);

	auto cdev = retrieve_dev(vnode->device.major, vnode->device.minor);

	if (!cdev) {
		serial2_printf("cdev not found\n");
		kfree2(d_);
		return;
	}

	int ret = ops->read(vnode, 16, d_, request_size);

	if (ret < 0) {
		serial2_printf("read failed: %d\n", ret);
		kfree2(d_);
		return;
	}

	struct iso9660_pvd* pvd = (struct iso9660_pvd*)(void*)d_;
	if (strncmp(pvd->id, "CD001", 5) == 0) {
		LOG2_INFO("VFS NOTIFY", "terdeteksi ISO9660 CD-ROM");

		boolean_t is_contain_root = false;
		{
			dentry_ptr mount_entry;
			vxnamei("/tmp/root", &mount_entry);
			vfs_mount(dentry, "ISO9660", mount_entry, 0);

			dentry_ptr out;
			if (resolve_dentry("/kernel.elf", mount_entry, &out,
			                   0) == VFS_OK) {

				auto full_path =
				    get_full_path_from_dentry(dentry);
				LOG2_INFO("VFS NOTIFY",
				          "found root filesystem at %s",
				          full_path->c_str);
				str_release(full_path);

				is_contain_root = true;
			}

			dentry_put(out);
			vfs_umount(mount_entry);
		}

		if (is_contain_root) {
			dentry_ptr mount_entry;
			vxnamei("/", &mount_entry);
			vfs_mount(dentry, "ISO9660", mount_entry, 0);
			notify_call("/vfs/root", VFS_NOTIFY_ROOT_FOUND, dentry);
		}
	}
	kfree2(d_);
}

__attribute__((unused)) static void vfs_notify_probe_handler(void* data,
                                                             void* ctx) {

	UNUSED(ctx);

	if (!data)
		return;

	dentry_ptr dentry = (dentry_ptr)data;

	if (!dentry->vnode)
		return;

	auto ops = (vops_blk_t*)dentry->vnode->ops;

	if (ops == 0)
		return;

	auto dev_major = dentry->vnode->device.major;

	serial2_printf("vnode major %d %x vdata %x\n", dev_major, ops,
	               ops->v_data);

	if (dev_major == DEV_MAJOR_CDROM) {
		detect_cd_filesystem(dentry, data, ctx);
	}
}

static void vfs_event_handler(uint32_t event, void* data, void* ctx) {
	UNUSED(data);
	UNUSED(ctx);
	UNUSED(event);

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
