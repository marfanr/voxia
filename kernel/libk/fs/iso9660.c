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

		if (memcmp(instance->fs->data.magic.magic, pvd->id,
		           instance->fs->data.magic.count) != 0) {
			LOG2_WARN("ISO9660", "invalid magic");
			kfree2(pvd);
			return -2;
		}

		if (pvd->type != 1) {
			LOG2_WARN("ISO9660", "invalid pvd type");
			kfree2(pvd);
			return -2;
		}

		auto root_dir = (struct iso9660_dir*)pvd->root_dir_record;

		struct iso9660_node* iso_node =
		    (struct iso9660_node*)kalloc(sizeof(struct iso9660_node));
		if (!iso_node) {
			kfree2(pvd);
			return -2;
		}
		iso_node->extent = root_dir->extent_le;
		iso_node->size = root_dir->size_le;
		iso_node->flags = root_dir->flags;

		if (!*out) {
			LOG2_WARN("ISO9660", "no dentry provided for root");
			kfree2(iso_node);
			kfree2(pvd);
			return -2;
		}

		if (!(*out)->vnode) {
			(*out)->vnode = create_and_attach_vnode();
			if (!(*out)->vnode) {
				kfree2(iso_node);
				kfree2(pvd);
				return -2;
			}
		}

		(*out)->vnode->vnode_private = iso_node;
		(*out)->vnode->type = VNODE_TYPE_DIR;
		(*out)->vnode->fs_instance = instance;

		LOG_DEBUG("ISO9660", "root dir extent=0x%x size=%d flags=%b",
		          iso_node->extent, iso_node->size, iso_node->flags);

		kfree2(pvd);
		return VFS_OK;
	}

	if (!parent->vnode) {
		LOG_WARN("ISO9660", "missing vnode");
		return -2;
	}

	if (!parent->vnode->vnode_private) {
		LOG_WARN("ISO9660", "missing vnode private data");
		return -2;
	}

	auto iso_node = (struct iso9660_node*)parent->vnode->vnode_private;
	LOG2_INFO("ISO9660", "parent %s size %d", parent->name->c_str,
	          iso_node->size);

	uint8_t* dir_buf = (uint8_t*)kalloc(iso_node->size);
	if (!dir_buf) {
		LOG2_ERROR("ISO9660", "failed to alloc dir buffer");
		return -2;
	}

	if (cdev_ops->read(cdev_ops->v_data, iso_node->extent, dir_buf,
	                   iso_node->size) < 0) {
		LOG2_ERROR("ISO9660", "failed to read dir extent");
		kfree2(dir_buf);
		return -3;
	}

	if (!*out) {
		*out = create_dentry(str(path), 0, parent);
	}

	if (!(*out)->vnode) {
		(*out)->vnode = create_and_attach_vnode();
	}

	if (!(*out)->vnode) {
		LOG2_ERROR("ISO9660", "failed to create vnode");
		kfree2(dir_buf);
		return -2;
	}

	{
		uintptr_t ptr = (uintptr_t)dir_buf;
		uintptr_t end = ptr + iso_node->size;

		while (ptr < end) {
			auto entry = (struct iso9660_dir*)ptr;

			if (entry->length == 0) {

				ptr = ((ptr / 2048) + 1) * 2048;
				continue;
			}

			uint8_t name_len = entry->name_len;
			char name[256];
			if (name_len > 255)
				name_len = 255;
			memcopy(name, entry->name, name_len);
			name[name_len] = '\0';

			for (int i = 0; i < name_len; i++) {
				if (name[i] == ';') {
					name[i] = '\0';
					name_len = (uint8_t)i;
					break;
				}
			}

			to_lowercase(name);

			if (strlen(name) == strlen(path) &&
			    strncmp(name, path, strlen(path)) == 0) {

				auto priv_data = (struct iso9660_node*)kalloc(
				    sizeof(struct iso9660_node));
				if (!priv_data) {
					kfree2(dir_buf);
					return -2;
				}

				priv_data->extent = entry->extent_le;
				priv_data->size = entry->size_le;

				priv_data->flags = entry->flags;

				if (entry->flags & iOS9660_DIR_FLAG) {
					(*out)->vnode->type = VNODE_TYPE_DIR;
				} else {
					(*out)->vnode->type = VNODE_TYPE_FILE;
					(*out)->vnode->size = entry->size_le;
				}

				serial2_printf(
				    "iso found %s extent=%u size=%u\n", name,
				    priv_data->extent, priv_data->size);

				(*out)->vnode->vnode_private = priv_data;
				(*out)->vnode->fs_instance = instance;
				kfree2(dir_buf);
				return VFS_OK;
			}

			ptr += entry->length;
		}
	}

	kfree2(dir_buf);
	return -1;
}

fs_operations_t* iso9660_file_operations(void) {
	_fs_ops.lookup = iso9660_lookup;
	return &_fs_ops;
}