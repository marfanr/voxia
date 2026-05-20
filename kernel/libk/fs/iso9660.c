#include "iso9660.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "type.h"
#include "vfs/enum.h"
#include "vfs/filesystem.h"
#include "vfs/vfs.h"
#include <str.h>
#include <vfs/dentry.h>
#include <vfs/dev.h>
#include <vfs/vnode.h>

static fs_operations_t _fs_ops = {0};

int iso9660_lookup(struct fs_instance* instance, char* path, dentry_ptr parent,
                   dentry_ptr* out) {

	if (!out) {
		return -1;
	}

	auto cdev_ops = instance->cdev->ops;
	if (!cdev_ops) {
		LOG2_WARN("ISO9660", "missing cdev ops");
		return -1;
	}

	// read root dir
	if (!parent) {

		auto pvd =
		    (struct iso9660_pvd*)kalloc(sizeof(struct iso9660_pvd));
		if (!pvd) {
			LOG2_WARN("ISO9660", "empty buffer");
			return -2;
		}

		if (cdev_ops->read(cdev_ops->v_data, 16, pvd,
		                   sizeof(struct iso9660_pvd)) < 0) {
			LOG2_WARN("ISO9660", "read failed");
			kfree2(pvd);
			return -2;
		}

		// TODO Validate magic and pvd type
		if (memcmp(instance->fs->data.magic.magic, pvd->id,
		           instance->fs->data.magic.count) != 0) {
			LOG2_WARN("ISO9660", "invalid magic");
			kfree2(pvd);
			return -2;
		};

		if (pvd->type != 1) {
			// assume parent
			LOG2_WARN("ISO9660", "invalid pvd type");
			kfree2(pvd);
			return -2;
		}

		auto root_dir = pvd->root_dir_record;
		auto file_flags = *(uint8_t*)((uintptr_t)root_dir + 25);
		uint32_t extent = *(uint32_t*)((uintptr_t)root_dir + 2);
		uint32_t size = *(uint32_t*)((uintptr_t)root_dir + 10);

		struct iso9660_node* iso_node =
		    (struct iso9660_node*)kalloc(sizeof(struct iso9660_node));
		iso_node->extent = extent;
		iso_node->size = size;
		iso_node->flags = file_flags;

		(*out)->vnode->vnode_private = iso_node;
		(*out)->vnode->type = VNODE_TYPE_DIR;
		(*out)->vnode->fs_instance = instance;

		LOG_DEBUG("ISO9660", "root dir at 0x%x %b", root_dir,
		          file_flags);

		return VFS_OK;
	}

	if (!parent->vnode) {
		LOG_WARN("ISO9660", "missing vnode");
		return -2;
	}

	// parent dentry internal data = exteent
	if (!parent->vnode->vnode_private) {
		LOG_WARN("ISO9660", "missing vnode private data");
		return -2;
	}

	auto iso_node = (struct iso9660_node*)parent->vnode->vnode_private;
	LOG2_INFO("ISO9660", "parent size %d", iso_node->size);
	uint8_t* dir_buf = (uint8_t*)kalloc(iso_node->size);

	if (cdev_ops->read(cdev_ops->v_data, iso_node->extent, dir_buf,
	                   iso_node->size) < 0) {
		LOG2_ERROR("ISO9660", "failed to read root dir");
		kfree2(dir_buf);
		return -3;
	}

	if (!*out) {
		*out = create_dentry(str(path), 0, parent);
	}

	auto vnode = (*out)->vnode;
	if (!vnode) {
		vnode = create_and_attach_vnode();
		(*out)->vnode = vnode;
	}

	{
		uintptr_t ptr = (uintptr_t)dir_buf;
		uintptr_t end = ptr + iso_node->size;
		(void)end;
		while (ptr < end) {
			auto entry = (struct iso9660_dir*)ptr;

			if (entry->length == 0) {
				ptr = ((ptr / 2048) + 1) * 2048;
				continue;
			}

			char* name = entry->name;
			to_lowercase(name);

			if (strncmp(name, path, strlen(path)) == 0) {
				auto priv_data = (struct iso9660_node*)kalloc(
				    sizeof(struct iso9660_node));
				priv_data->extent = entry->extent_le;
				priv_data->size = entry->length;
				priv_data->flags = entry->flags;

				if (entry->flags & iOS9660_DIR_FLAG) {
					(*out)->vnode->type = VNODE_TYPE_DIR;
				} else {
					(*out)->vnode->type = VNODE_TYPE_FILE;
					(*out)->vnode->size = entry->length;
				}

				(*out)->vnode->vnode_private = priv_data;
				(*out)->vnode->fs_instance = instance;
				return VFS_OK;
			}

			ptr += entry->length;
		}
	}
	return 0;
}

fs_operations_t* iso9660_file_operations(void) {
	_fs_ops.lookup = iso9660_lookup;
	return &_fs_ops;
}