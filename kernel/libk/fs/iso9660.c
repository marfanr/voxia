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
static vops_file_t _file_ops = {0};
static vops_lnk_t _lnk_ops = {0};

struct iso9660_internal_data {
	uint32_t extent;
	uint32_t size;
	uint8_t flags;
};

static int iso9660_get_rr_symlink(struct iso9660_dir* entry, char* out_target) {
	/* SUA started after fixed header (33 byte) + name_len, align into 2
	 * byte */
	uint8_t name_len = entry->name_len;
	uint8_t sua_offset = 33 + name_len;
	if (sua_offset % 2 != 0)
		sua_offset += 1; /* padding byte */

	uint8_t total_len = entry->length;
	if (sua_offset >= total_len)
		return 0; /* no SUA */

	uint8_t* sua = (uint8_t*)entry + sua_offset;
	uint8_t sua_len = total_len - sua_offset;
	uint8_t i = 0;
	int target_len = 0;

	out_target[0] = '\0';

	while (i + 4 <= sua_len) {
		uint8_t sig0 = sua[i];
		uint8_t sig1 = sua[i + 1];
		uint8_t len = sua[i + 2];
		/* uint8_t ver = sua[i + 3]; */

		if (len < 4)
			break;

		/* Rock Ridge SL (Symbolic Link) */
		if (sig0 == 'S' && sig1 == 'L') {
			uint8_t comp_offset = 5;

			while (comp_offset < len) {
				uint8_t c_flags = sua[i + comp_offset];
				uint8_t c_len = sua[i + comp_offset + 1];
				uint8_t* c_content = &sua[i + comp_offset + 2];

				if (c_flags & 0x08) { /* ROOT */
					out_target[target_len++] = '/';
				} else if (c_flags & 0x02) { /* CURRENT */
					out_target[target_len++] = '.';
					out_target[target_len++] = '/';
				} else if (c_flags & 0x04) { /* PARENT */
					out_target[target_len++] = '.';
					out_target[target_len++] = '.';
					out_target[target_len++] = '/';
				} else {
					/* Komponen nama reguler */
					memcopy(&out_target[target_len],
					        c_content, c_len);
					target_len += c_len;

					if (!(c_flags & 0x01) &&
					    (comp_offset + 2 + c_len < len)) {
						out_target[target_len++] = '/';
					}
				}

				comp_offset += 2 + c_len;
			}

			out_target[target_len] = '\0';
			return target_len;
		}

		i += len;
	}

	return 0;
}

static int iso9660_get_rr_name(struct iso9660_dir* entry, char* out_name) {
	/* SUA started after fixed header (33 byte) + name_len, align into 2
	 * byte */
	uint8_t name_len = entry->name_len;
	uint8_t sua_offset = 33 + name_len;
	if (sua_offset % 2 != 0)
		sua_offset += 1; /* padding byte */

	uint8_t total_len = entry->length;
	if (sua_offset >= total_len)
		return 0; /* no SUA */

	uint8_t* sua = (uint8_t*)entry + sua_offset;
	uint8_t sua_len = total_len - sua_offset;
	uint8_t i = 0;

	while (i + 4 <= sua_len) {
		uint8_t sig0 = sua[i];
		uint8_t sig1 = sua[i + 1];
		uint8_t len = sua[i + 2];
		/* uint8_t ver = sua[i + 3]; */

		if (len < 4)
			break;

		/* Rock Ridge NM (Alternate Name) */
		if (sig0 == 'N' && sig1 == 'M') {
			uint8_t flags = sua[i + 4];
			if (flags & 0x06)
				return 0;

			uint8_t nm_len =
			    len - 5; /* 2 sig + 1 len + 1 ver + 1 flags */
			if (nm_len == 0 || nm_len > 255)
				return 0;

			memcopy(out_name, &sua[i + 5], nm_len);
			out_name[nm_len] = '\0';
			return nm_len;
		}

		i += len;
	}
	return 0;
}

static int iso9660_get_rr_mode(struct iso9660_dir* entry, uint16_t* out_mode) {
	uint8_t name_len = entry->name_len;
	uint8_t sua_offset = 33 + name_len;
	if (sua_offset % 2 != 0)
		sua_offset += 1;

	uint8_t total_len = entry->length;
	if (sua_offset >= total_len)
		return 0;

	uint8_t* sua = (uint8_t*)entry + sua_offset;
	uint8_t sua_len = total_len - sua_offset;
	uint8_t i = 0;

	while (i + 4 <= sua_len) {
		uint8_t sig0 = sua[i];
		uint8_t sig1 = sua[i + 1];
		uint8_t len = sua[i + 2];

		if (len < 4)
			break;

		if (sig0 == 'P' && sig1 == 'X' && len >= 12) {
			uint32_t mode = *(uint32_t*)(void *)&sua[i + 4];
			*out_mode = mode & 0xFFFF;
			return 1;
		}
		i += len;
	}
	return 0;
}

int iso9660_lookup(struct fs_instance* instance, char* path, dentry_ptr parent,
                   dentry_ptr* out) {

	if (!out) {
		return -1;
	}

	auto block_dentry = instance->block_dentry;
	if (!block_dentry) {
		LOG2_WARN("ISO9660", "missing block dentry");
		return -1;
	}

	auto block_vnode = block_dentry->vnode;
	if (!block_vnode) {
		LOG2_WARN("ISO9660", "missing block vnode");
		return -1;
	}

	auto ops = (vops_blk_t*)block_vnode->ops;
	if (!ops) {
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

		if (ops->read(block_vnode, 16, pvd,
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

		struct iso9660_internal_data* iso_node =
		    (struct iso9660_internal_data*)kalloc(
		        sizeof(struct iso9660_internal_data));
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
		(*out)->vnode->ops = iso9660_file_operations();
		(*out)->vnode->fs_instance = instance;

		uint16_t rr_mode = 0;
		if (iso9660_get_rr_mode(root_dir, &rr_mode)) {
			(*out)->vnode->permission = rr_mode;
		} else {
			(*out)->vnode->permission = 0555;
		}

		LOG_DEBUG("ISO9660", "root dir extent=0x%x size=%d",
		          iso_node->extent, iso_node->size);

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

	auto iso_node =
	    (struct iso9660_internal_data*)parent->vnode->vnode_private;
	LOG2_INFO("ISO9660", "parent %s size %d", parent->name->c_str,
	          iso_node->size);

	uint8_t* dir_buf = (uint8_t*)kalloc(iso_node->size);
	if (!dir_buf) {
		LOG2_ERROR("ISO9660", "failed to alloc dir buffer");
		return -2;
	}

	if (ops->read(block_vnode, iso_node->extent, dir_buf, iso_node->size) <
	    0) {
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
		uintptr_t base = (uintptr_t)dir_buf;
		uintptr_t ptr = base;
		uintptr_t end = base + iso_node->size;

		while (ptr < end) {
			auto entry = (struct iso9660_dir*)ptr;

			if (entry->length == 0) {
				uintptr_t offset = ptr - base;
				uintptr_t next_sector =
				    ((offset / 2048) + 1) * 2048;
				if (next_sector >= iso_node->size)
					break;
				ptr = base + next_sector;
				continue;
			}

			if (entry->name_len == 1 && (entry->name[0] == 0x00 ||
			                             entry->name[0] == 0x01)) {
				ptr += entry->length;
				continue;
			}

			char name[256];
			auto name_len = iso9660_get_rr_name(entry, name);

			if (name_len <= 0) {
				uint8_t iso_len = entry->name_len;
				if (iso_len > 255)
					iso_len = 255;
				memcopy(name, entry->name, iso_len);
				name[iso_len] = '\0';
				name_len = iso_len;

				for (int i = 0; i < name_len; i++) {
					if (name[i] == ';') {
						name[i] = '\0';
						name_len = i;
						break;
					}
				}

				to_lowercase(name);
			}
			
			if (strlen(name) == strlen(path) &&
			    strncmp(name, path, strlen(path)) == 0) {

				serial2_printf("file %s flags %b (%d)\n", name,
				               (uint8_t)entry->flags,
				               entry->size_be);

				char symlink_target[256];
				int sym_len = iso9660_get_rr_symlink(
				    entry, symlink_target);

				uint16_t rr_mode = 0;
				if (iso9660_get_rr_mode(entry, &rr_mode)) {
					(*out)->vnode->permission = rr_mode;
				} else {
					if (entry->flags & iOS9660_DIR_FLAG) {
						(*out)->vnode->permission = 0555;
					} else {
						(*out)->vnode->permission = 0444;
					}
				}

				if (sym_len > 0) {
					serial2_printf(
					    "found symlink at %s (%d)\n",
					    symlink_target, sym_len);
					(*out)->vnode->type = VNODE_TYPE_LNK;
					(*out)->vnode->vnode_private =
					    kalloc((size_t)sym_len + 1);
					memcopy((*out)->vnode->vnode_private,
					        symlink_target,
					        (size_t)sym_len);
					((char*)(*out)->vnode->vnode_private)[sym_len] = '\0';

					(*out)->vnode->ops =
					    iso9660_lnk_operations();
					(*out)->vnode->fs_instance = instance;
				} else {
					auto priv_data =
					    (struct iso9660_internal_data*)
					        kalloc(sizeof(
					            struct
					            iso9660_internal_data));
					if (!priv_data) {
						kfree2(dir_buf);
						return -2;
					}

					priv_data->extent = entry->extent_le;
					priv_data->size = entry->size_le;
					priv_data->flags = entry->flags;

					if (entry->flags & iOS9660_DIR_FLAG) {
						(*out)->vnode->type =
						    VNODE_TYPE_DIR;
						(*out)->vnode->ops =
						    iso9660_file_operations();
					} else {
						(*out)->vnode->type =
						    VNODE_TYPE_FILE;
						(*out)->vnode->ops =
						    iso9660_file_operations();
						(*out)->vnode->size =
						    entry->size_le;
					}

					(*out)->vnode->vnode_private =
					    priv_data;
					(*out)->vnode->fs_instance = instance;
				}
				kfree2(dir_buf);
				return VFS_OK;
			}

			ptr += entry->length;
		}
	}

	kfree2(dir_buf);
	return -1;
}

int iso9660_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	UNUSED(offset);

	if (!vnode || !buf || !len)
		return -1;

	auto iso_node = (struct iso9660_internal_data*)vnode->vnode_private;
	if (!iso_node)
		return -2;

	auto block_dentry = vnode->fs_instance->block_dentry;
	if (!block_dentry)
		return -2;

	auto block_vnode = block_dentry->vnode;
	if (!block_vnode)
		return -2;

	auto ops = (vops_blk_t*)block_vnode->ops;
	if (!ops)
		return -3;

	if (offset >= iso_node->size) return 0;
	size_t read_len = len;
	if (offset + read_len > iso_node->size) {
		read_len = iso_node->size - offset;
	}
	
	size_t start_lba = iso_node->extent + (offset / 2048);
	size_t byte_offset_in_lba = offset % 2048;
	size_t lba_count = (byte_offset_in_lba + read_len + 2047) / 2048;
	size_t bytes_to_read = lba_count * 2048;

	void* temp_buf = kalloc(bytes_to_read);
	if (!temp_buf) return -1;

	if (ops->read(block_vnode, start_lba, temp_buf, bytes_to_read) < 0) {
		LOG2_ERROR("ISO9660", "failed to read file extent");
		kfree2(temp_buf);
		return -3;
	}
	
	memcopy(buf, (void*)((uintptr_t)temp_buf + byte_offset_in_lba), read_len);
	kfree2(temp_buf);

	return (int)read_len;
}

int iso9660_readlink(vnode_t* vnode, char* buf, size_t len) {
	auto priv_data = (char*)vnode->vnode_private;
	serial2_printf("%s \n", (char*)priv_data);
	if (!priv_data)
		return -1;

	auto len_ = strlen(priv_data) > len ? len : strlen(priv_data);
	memcopy(buf, priv_data, len_);
	if (len_ < len) {
		buf[len_] = '\0';
	}

	return (int)len_;
}

static int iso9660_readdir(vnode_t* vnode, void* buf, size_t len, uint64_t* pos) {
	if (!vnode || !vnode->vnode_private) return -1;
	auto iso_node = (struct iso9660_internal_data*)vnode->vnode_private;
	auto block_vnode = vnode->fs_instance->block_dentry->vnode;
	auto ops = (vops_blk_t*)block_vnode->ops;

	uint8_t* dir_buf = (uint8_t*)kalloc(iso_node->size);
	if (!dir_buf) return -1;
	
	if (ops->read(block_vnode, iso_node->extent, dir_buf, iso_node->size) < 0) {
		kfree2(dir_buf);
		return -1;
	}

	uintptr_t base = (uintptr_t)dir_buf;
	uintptr_t ptr = base + *pos;
	uintptr_t end = base + iso_node->size;

	int bytes_written = 0;
	struct dirent* user_dirent = (struct dirent*)buf;
	
	while (ptr < end && bytes_written < (int)len) {
		auto entry = (struct iso9660_dir*)ptr;
		if (entry->length == 0) {
			uintptr_t offset = ptr - base;
			uintptr_t next_sector = ((offset / 2048) + 1) * 2048;
			if (next_sector >= iso_node->size) break;
			ptr = base + next_sector;
			continue;
		}

		char name[256];
		int name_len = 0;

		if (entry->name_len == 1 && entry->name[0] == 0x00) {
			name[0] = '.'; name[1] = '\0'; name_len = 1;
		} else if (entry->name_len == 1 && entry->name[0] == 0x01) {
			name[0] = '.'; name[1] = '.'; name[2] = '\0'; name_len = 2;
		} else {
			name_len = iso9660_get_rr_name(entry, name);
			if (name_len <= 0) {
				uint8_t iso_len = entry->name_len;
				if (iso_len > 255) iso_len = 255;
				memcopy(name, entry->name, iso_len);
				name[iso_len] = '\0';
				name_len = iso_len;
				for (int i = 0; i < name_len; i++) {
					if (name[i] == ';') {
						name[i] = '\0';
						name_len = i;
						break;
					}
				}
			}
		}

		int dirent_size = (int)(sizeof(struct dirent)) + name_len + 1;
		dirent_size = (dirent_size + 7) & ~7;
		
		if (bytes_written + dirent_size > (int)len) {
			break;
		}

		struct dirent current_entry = {0};
		current_entry.d_ino = entry->extent_le;
		current_entry.d_off = ptr - base;
		current_entry.d_reclen = (uint16_t)dirent_size;
		current_entry.d_type = (entry->flags & 2) ? DT_DIR : DT_REG;
		
		memcopy((void*)user_dirent, &current_entry, sizeof(struct dirent));
		memcopy((void*)user_dirent->d_name, name, (size_t)(name_len + 1));

		user_dirent = (struct dirent*)((uintptr_t)user_dirent + (uintptr_t)dirent_size);
		bytes_written += dirent_size;
		ptr += entry->length;
	}

	*pos = ptr - base;
	kfree2(dir_buf);
	return bytes_written;
}

vops_file_t* iso9660_file_operations(void) {
	_file_ops.read = iso9660_read;
	_file_ops.readdir = iso9660_readdir;
	return &_file_ops;
}

fs_operations_t* iso9660_fs_operations(void) {
	_fs_ops.lookup = iso9660_lookup;
	return &_fs_ops;
}

vops_lnk_t* iso9660_lnk_operations(void) {
	_lnk_ops.readlink = iso9660_readlink;
	return &_lnk_ops;
}