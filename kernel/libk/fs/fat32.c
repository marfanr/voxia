#include "fat32.h"
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

#define MAX_LONG_FILENAME 260
#define HIGH_SURROGATE_START 0xD800
#define HIGH_SURROGATE_END 0xDBFF
#define LOW_SURROGATE_START 0xDC00
#define LOW_SURROGATE_END 0xDFFF

// refer RFC 2781, RFC 3629, RFC 2279
// https://unicode.org/mail-arch/unicode-ml/y2003-m02/att-0467/01-The_Algorithm_to_Valide_an_UTF-8_String
// TODO: will be refactor
static void fat32_utf16_to_utf8(const uint16_t* utf16, char* utf8,
                                int max_utf8_len) {
	int out_idx = 0;
	for (int i = 0;
	     i < MAX_LONG_FILENAME && utf16[i] != 0 && utf16[i] != 0xFFFF;
	     i++) {
		uint32_t cp = utf16[i];
		/* decode utf-16 */
		if (cp >= HIGH_SURROGATE_START && cp <= HIGH_SURROGATE_END) {
			uint32_t low = utf16[i + 1];
			if (low >= LOW_SURROGATE_START &&
			    low <= LOW_SURROGATE_END) {
				cp = 0x10000 +
				     ((cp - HIGH_SURROGATE_START) << 10) +
				     (low - LOW_SURROGATE_START);
				i++;
			}
		}
		// detect it is ASCII
		if (cp < 0x80) {
			if (out_idx + 1 >= max_utf8_len)
				break;
			utf8[out_idx++] = (char)cp;
			// 2 byte utf-8
		} else if (cp < 0x800) {
			if (out_idx + 2 >= max_utf8_len)
				break;
			utf8[out_idx++] = (char)(0xC0 | (cp >> 6));
			utf8[out_idx++] = (char)(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			if (out_idx + 3 >= max_utf8_len)
				break;
			utf8[out_idx++] = (char)(0xE0 | (cp >> 12));
			utf8[out_idx++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			utf8[out_idx++] = (char)(0x80 | (cp & 0x3F));
		} else {
			if (out_idx + 4 >= max_utf8_len)
				break;
			utf8[out_idx++] = (char)(0xF0 | (cp >> 18));
			utf8[out_idx++] = (char)(0x80 | ((cp >> 12) & 0x3F));
			utf8[out_idx++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			utf8[out_idx++] = (char)(0x80 | (cp & 0x3F));
		}
	}
	utf8[out_idx] = '\0';
}

static int fat32_utf8_to_utf16(const char* utf8, uint16_t* utf16,
                               int max_utf16_len) {
	int out_idx = 0;
	int in_idx = 0;
	while (utf8[in_idx] != '\0') {
		uint32_t cp = 0;
		uint8_t c = (uint8_t)utf8[in_idx];
		int extra_bytes = 0;
		if (c < 0x80) {
			cp = c;
			in_idx++;
		} else if ((c & 0xE0) == 0xC0) {
			cp = c & 0x1F;
			extra_bytes = 1;
			in_idx++;
		} else if ((c & 0xF0) == 0xE0) {
			cp = c & 0x0F;
			extra_bytes = 2;
			in_idx++;
		} else if ((c & 0xF8) == 0xF0) {
			cp = c & 0x07;
			extra_bytes = 3;
			in_idx++;
		} else {
			cp = c;
			in_idx++;
		}
		for (int i = 0; i < extra_bytes; i++) {
			if (utf8[in_idx] == '\0')
				break;
			uint8_t b = (uint8_t)utf8[in_idx];
			if ((b & 0xC0) == 0x80) {
				cp = (cp << 6) | (b & 0x3F);
				in_idx++;
			} else {
				break;
			}
		}
		if (cp < 0x10000) {
			if (out_idx >= max_utf16_len)
				break;
			utf16[out_idx++] = (uint16_t)cp;
		} else {
			if (out_idx + 1 >= max_utf16_len)
				break;
			cp -= 0x10000;
			utf16[out_idx++] = (uint16_t)(0xD800 + (cp >> 10));
			utf16[out_idx++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
		}
	}
	if (out_idx < max_utf16_len) {
		utf16[out_idx] = 0;
	}
	return out_idx;
}

static fs_operations_t _fs_ops = {0};
static vops_file_t _file_ops = {0};
static vops_lnk_t _lnk_ops = {0};

struct fat32_internal_data {
	uint32_t cluster;
	uint32_t size;
	uint32_t size_in_sectors;
	uint8_t flags;

	uint32_t first_data_sector;
	uint32_t sectors_per_cluster;
	uint32_t bytes_per_sector;
	uint32_t fat_start_sector;
	uint32_t dir_sector;
	uint32_t dir_offset;

	uint32_t fsinfo_sector;
	struct fat32_fsinfo fsinfo;
	uint8_t fats_count;
	uint16_t backup_boot_sector;
	uint32_t data_sectors;
};

// MS FAT Spec Section 4 (Page 15): FAT Table - Finding the next cluster in the
// chain
static uint32_t fat32_get_next_cluster(vnode_t* block_vnode,
                                       struct fat32_internal_data* info,
                                       uint32_t current_cluster) {
	vops_blk_t* ops = (vops_blk_t*)block_vnode->ops;
	uint32_t fat_offset = current_cluster * 4;
	uint32_t fat_sector =
	    info->fat_start_sector + (fat_offset / info->bytes_per_sector);
	uint32_t ent_offset = fat_offset % info->bytes_per_sector;

	uint8_t* sec_buf = (uint8_t*)kalloc(info->bytes_per_sector);
	if (!sec_buf)
		return 0x0FFFFFF8;

	if (ops->read(block_vnode, fat_sector, sec_buf,
	              info->bytes_per_sector) < 0) {
		kfree2(sec_buf);
		return 0x0FFFFFF8;
	}

	uint32_t next_cluster;
	memcopy(&next_cluster, &sec_buf[ent_offset], sizeof(uint32_t));
	next_cluster &= 0x0FFFFFFF;

	kfree2(sec_buf);
	return next_cluster;
}

// MS FAT Spec Section 4 (Page 14): Calculating FirstSectorofCluster
static uint32_t fat32_cluster_to_sector(struct fat32_internal_data* info,
                                        uint32_t cluster) {
	return info->first_data_sector +
	       (cluster - 2) * info->sectors_per_cluster;
}

int fat32_lookup(struct fs_instance* instance, char* path, dentry_ptr parent,
                 dentry_ptr* out) {

	if (!out) {
		return -1;
	}

	auto block_dentry = instance->block_dentry;
	if (!block_dentry) {
		LOG2_WARN("FAT32", "missing block dentry");
		return -1;
	}

	auto block_vnode = block_dentry->vnode;
	if (!block_vnode) {
		LOG2_WARN("FAT32", "missing block vnode");
		return -1;
	}

	auto ops = (vops_blk_t*)block_vnode->ops;
	if (!ops) {
		LOG2_WARN("FAT32", "missing cdev ops");
		return -1;
	}

	if (!parent) {
		auto bpb = (struct fat32_bpb*)kalloc(512);
		if (!bpb) {
			LOG2_WARN("FAT32", "empty buffer");
			return -2;
		}

		if (ops->read(block_vnode, 0, bpb, 512) < 0) {
			LOG2_WARN("FAT32", "read failed");
			kfree2(bpb);
			return -2;
		}

		if (memcmp(instance->fs->data.magic.magic, bpb->system_id,
		           instance->fs->data.magic.count) != 0) {
			LOG2_WARN("FAT32", "invalid magic");
			kfree2(bpb);
			return -2;
		}

		struct fat32_internal_data* fat_node =
		    (struct fat32_internal_data*)kalloc(
		        sizeof(struct fat32_internal_data));
		if (!fat_node) {
			kfree2(bpb);
			return -2;
		}

		auto fsinfo_sector = bpb->fsinfo_sector;
		{
			uint8_t* fsinfo_buf =
			    (uint8_t*)kalloc(bpb->bytes_per_sector);
			ops->read(block_vnode, fsinfo_sector, fsinfo_buf,
			          bpb->bytes_per_sector);
			auto fsinfo = (struct fat32_fsinfo*)fsinfo_buf;
			if (fsinfo->lead_signature == FAT32_LEAD_SIG &&
			    fsinfo->struct_signature == FAT32_STRUCT_SIG &&
			    fsinfo->trail_signature == FAT32_TRAIL_SIG) {
				serial2_printf("FSinfo found at sector : %d\n",
				               fsinfo_sector);
				serial2_printf("free cluster count : %d\n",
				               fsinfo->free_cluster_count);

				fat_node->fsinfo_sector = fsinfo_sector;
				memcopy(&fat_node->fsinfo, fsinfo,
				        sizeof(struct fat32_fsinfo));
			}
			kfree2(fsinfo_buf);
		}

		// MS FAT Spec Section 3.2 (Page 14): FirstDataSector &
		// RootDirSectors calculation
		fat_node->bytes_per_sector = bpb->bytes_per_sector;
		if (fat_node->bytes_per_sector == 0)
			fat_node->bytes_per_sector = 512;

		fat_node->sectors_per_cluster = bpb->sectors_per_cluster;
		fat_node->fat_start_sector = bpb->reserved_sectors;
		fat_node->fats_count = bpb->fats_count ? bpb->fats_count : 2;
		serial2_printf("fats count : %d\n", fat_node->fats_count);

		fat_node->backup_boot_sector = bpb->backup_boot_sector;

		uint32_t fat_size = bpb->sectors_per_fat_16
		                        ? bpb->sectors_per_fat_16
		                        : bpb->sectors_per_fat_32;
		fat_node->size_in_sectors = fat_size;

		auto total_sector = bpb->total_sectors_16
		                        ? bpb->total_sectors_16
		                        : bpb->total_sectors_32;
		auto data_sector = total_sector - bpb->reserved_sectors -
		                   (bpb->fats_count * fat_size);
		fat_node->data_sectors = data_sector;

		uint32_t root_dir_sectors = ((bpb->root_dir_entries * 32) +
		                             (fat_node->bytes_per_sector - 1)) /
		                            fat_node->bytes_per_sector;

		fat_node->first_data_sector = bpb->reserved_sectors +
		                              (bpb->fats_count * fat_size) +
		                              root_dir_sectors;

		fat_node->cluster = bpb->root_cluster;
		if (fat_node->cluster == 0)
			fat_node->cluster = 2; // Default for FAT32 root

		fat_node->size = 0;
		fat_node->flags = FAT_ATTR_DIRECTORY;
		fat_node->dir_sector = 0;
		fat_node->dir_offset = 0;

		if (!*out) {
			LOG2_WARN("FAT32", "no dentry provided for root");
			kfree2(fat_node);
			kfree2(bpb);
			return -2;
		}

		if (!(*out)->vnode) {
			(*out)->vnode = create_and_attach_vnode();
			if (!(*out)->vnode) {
				kfree2(fat_node);
				kfree2(bpb);
				return -2;
			}
		}

		(*out)->vnode->vnode_private = fat_node;
		(*out)->vnode->type = VNODE_TYPE_DIR;
		(*out)->vnode->fs_instance = instance;
		(*out)->vnode->permission = 0555;

		LOG_DEBUG("FAT32", "root dir cluster=0x%x", fat_node->cluster);

		kfree2(bpb);
		return VFS_OK;
	}

	if (!parent->vnode) {
		LOG_WARN("FAT32", "missing vnode");
		return -2;
	}

	if (!parent->vnode->vnode_private) {
		LOG_WARN("FAT32", "missing vnode private data");
		return -2;
	}

	auto fat_node =
	    (struct fat32_internal_data*)parent->vnode->vnode_private;
	LOG2_INFO("FAT32", "parent %s cluster 0x%x", parent->name->c_str,
	          fat_node->cluster);

	uint32_t bytes_per_cluster =
	    fat_node->sectors_per_cluster * fat_node->bytes_per_sector;
	uint8_t* dir_buf = (uint8_t*)kalloc(bytes_per_cluster);
	if (!dir_buf) {
		LOG2_ERROR("FAT32", "failed to alloc dir buffer");
		return -2;
	}

	uint32_t current_cluster = fat_node->cluster;

	int found = 0;

	while (current_cluster < 0x0FFFFFF8) {
		uint32_t sector =
		    fat32_cluster_to_sector(fat_node, current_cluster);

		if (ops->read(block_vnode, sector, dir_buf, bytes_per_cluster) <
		    0) {
			LOG2_ERROR("FAT32", "failed to read dir cluster");
			break;
		}

		uintptr_t base = (uintptr_t)dir_buf;
		uintptr_t ptr = base;
		uintptr_t end = base + bytes_per_cluster;

		uint16_t lfn_utf16[MAX_LONG_FILENAME] = {0};
		int lfn_active = 0;

		while (ptr < end) {
			struct fat32_dir_entry* entry =
			    (struct fat32_dir_entry*)ptr;

			if (entry->name[0] == 0x00) {
				current_cluster = 0x0FFFFFFF;
				break;
			}

			if ((uint8_t)entry->name[0] == 0xE5) {
				ptr += 32;
				continue;
			}

			if (entry->attr == FAT_ATTR_LFN) {
				struct fat32_lfn_entry* lfn =
				    (struct fat32_lfn_entry*)ptr;
				int index = (lfn->order & 0x1F) - 1;
				if (index >= 0 && index < 20) {
					uint16_t* p = lfn_utf16 + index * 13;
					p[0] = lfn->name1[0];
					p[1] = lfn->name1[1];
					p[2] = lfn->name1[2];
					p[3] = lfn->name1[3];
					p[4] = lfn->name1[4];
					p[5] = lfn->name2[0];
					p[6] = lfn->name2[1];
					p[7] = lfn->name2[2];
					p[8] = lfn->name2[3];
					p[9] = lfn->name2[4];
					p[10] = lfn->name2[5];
					p[11] = lfn->name3[0];
					p[12] = lfn->name3[1];
					lfn_active = 1;
				}
				ptr += 32;
				continue;
			}

			char name[256] = {0};
			char sfn_name[16];
			int has_lfn = lfn_active;
			if (has_lfn) {
				lfn_active = 0;
				fat32_utf16_to_utf8(lfn_utf16, name, 256);
				memset(lfn_utf16, 0, sizeof(lfn_utf16));
			}

			int name_idx = 0;
			for (int i = 0; i < 8; i++) {
				if (entry->name[i] != ' ') {
					sfn_name[name_idx++] = entry->name[i];
				}
			}
			if (entry->name[8] != ' ') {
				sfn_name[name_idx++] = '.';
				for (int i = 8; i < 11; i++) {
					if (entry->name[i] != ' ') {
						sfn_name[name_idx++] =
						    entry->name[i];
					}
				}
			}
			sfn_name[name_idx] = '\0';

			to_lowercase(sfn_name);
			if (has_lfn)
				to_lowercase(name);

			int match = 0;
			if (has_lfn && strlen(name) == strlen(path) &&
			    strncmp(name, path, strlen(path)) == 0)
				match = 1;
			if (!match && strlen(sfn_name) == strlen(path) &&
			    strncmp(sfn_name, path, strlen(path)) == 0)
				match = 1;

			if (match) {
				serial2_printf("file %s flags %b (%d)\n",
				               has_lfn ? name : sfn_name,
				               entry->attr, entry->size);
				if (!*out) {
					*out =
					    create_dentry(str(path), 0, parent);
				}

				if (!(*out)->vnode) {
					(*out)->vnode =
					    create_and_attach_vnode();
				}

				if (!(*out)->vnode) {
					LOG2_ERROR("FAT32",
					           "failed to create vnode");
					kfree2(dir_buf);
					return -2;
				}

				auto priv_data =
				    (struct fat32_internal_data*)kalloc(
				        sizeof(struct fat32_internal_data));
				if (!priv_data) {
					kfree2(dir_buf);
					return -2;
				}

				priv_data->cluster =
				    ((uint32_t)entry->cluster_high << 16) |
				    entry->cluster_low;
				priv_data->size = entry->size;
				priv_data->flags = entry->attr;

				priv_data->first_data_sector =
				    fat_node->first_data_sector;
				priv_data->sectors_per_cluster =
				    fat_node->sectors_per_cluster;
				priv_data->bytes_per_sector =
				    fat_node->bytes_per_sector;
				priv_data->fat_start_sector =
				    fat_node->fat_start_sector;
				priv_data->size_in_sectors =
				    fat_node->size_in_sectors;
				priv_data->fats_count = fat_node->fats_count;
				priv_data->backup_boot_sector =
				    fat_node->backup_boot_sector;
				priv_data->dir_sector = sector;
				priv_data->dir_offset = (uint32_t)(ptr - base);
				priv_data->fsinfo_sector =
				    fat_node->fsinfo_sector;
				priv_data->data_sectors =
				    fat_node->data_sectors;

				memcopy(&priv_data->fsinfo, &fat_node->fsinfo,
				        sizeof(struct fat32_fsinfo));

				if (entry->attr & FAT_ATTR_DIRECTORY) {
					(*out)->vnode->type = VNODE_TYPE_DIR;
					(*out)->vnode->permission = 0555;
				} else {
					(*out)->vnode->type = VNODE_TYPE_FILE;
					(*out)->vnode->ops =
					    fat32_file_operations();
					(*out)->vnode->size = entry->size;
					(*out)->vnode->permission = 0444;
				}

				(*out)->vnode->vnode_private = priv_data;
				(*out)->vnode->fs_instance = instance;

				found = 1;
				break;
			}

			ptr += 32;
		}

		if (found)
			break;

		if (current_cluster < 0x0FFFFFF8) {
			current_cluster = fat32_get_next_cluster(
			    block_vnode, fat_node, current_cluster);
		}
	}

	kfree2(dir_buf);
	if (found)
		return VFS_OK;
	return -1;
}

static void fat32_write_fsinfo(vnode_t* block_vnode,
                               struct fat32_internal_data* info) {
	vops_blk_t* ops = (vops_blk_t*)block_vnode->ops;
	ops->write(block_vnode, info->fsinfo_sector, &info->fsinfo,
	           sizeof(struct fat32_fsinfo));
	if (info->backup_boot_sector != 0 &&
	    info->backup_boot_sector != 0xFFFF) {
		ops->write(block_vnode,
		           info->backup_boot_sector + info->fsinfo_sector,
		           &info->fsinfo, sizeof(struct fat32_fsinfo));
	}
}

static uint32_t fat32_allocate_cluster(vnode_t* block_vnode,
                                       struct fat32_internal_data* fat_node) {
	vops_blk_t* ops = block_vnode->ops;

	uint32_t* fat_buf = kalloc(fat_node->bytes_per_sector);

	uint32_t max_cluster =
	    fat_node->data_sectors / fat_node->sectors_per_cluster;

	uint32_t start = fat_node->fsinfo.next_free_cluster;

	if (start < 2 || start >= max_cluster + 2)
		start = 2;

	for (uint32_t i = 0; i < max_cluster; i++) {
		uint32_t cluster = ((start - 2 + i) % max_cluster) + 2;

		uint32_t sector = fat_node->fat_start_sector +
		                  (cluster * 4) / fat_node->bytes_per_sector;

		if (ops->read(block_vnode, sector, fat_buf,
		              fat_node->bytes_per_sector) < 0)
			break;

		uint32_t offset = cluster % (fat_node->bytes_per_sector / 4);

		if ((fat_buf[offset] & 0x0FFFFFFF) == 0) {
			fat_buf[offset] = 0x0FFFFFFF;

			for (uint32_t f = 0; f < fat_node->fats_count; f++) {
				ops->write(block_vnode,
				           sector +
				               (f * fat_node->size_in_sectors),
				           fat_buf, fat_node->bytes_per_sector);
			}

			uint32_t next = cluster + 1;

			if (next >= max_cluster + 2)
				next = 2;

			fat_node->fsinfo.next_free_cluster = next;

			if (fat_node->fsinfo.free_cluster_count != 0xFFFFFFFF)
				fat_node->fsinfo.free_cluster_count--;

			fat32_write_fsinfo(block_vnode, fat_node);
			kfree2(fat_buf);

			return cluster;
		}
	}

	kfree2(fat_buf);
	return 0;
}

static uint8_t fat32_sfn_checksum(const uint8_t* short_name) {
	uint8_t sum = 0;
	for (int i = 0; i < 11; i++) {
		sum = (uint8_t)((((sum & 1) ? 0x80 : 0) | (sum >> 1)) +
		                short_name[i]);
	}
	return sum;
}

static int fat32_create(struct fs_instance* instance, char* path,
                        dentry_ptr parent, dentry_ptr* out) {
	auto block_vnode = instance->block_dentry->vnode;
	auto ops = (vops_blk_t*)block_vnode->ops;
	auto parent_fat =
	    (struct fat32_internal_data*)parent->vnode->vnode_private;

	char name83[11];
	memset(name83, ' ', 11);
	int idx = 0;
	char* p = path;
	while (*p && *p != '.' && idx < 8) {
		name83[idx++] =
		    (*p >= 'a' && *p <= 'z') ? (char)(*p - 'a' + 'A') : *p;
		p++;
	}
	if (*p == '.') {
		p++;
		idx = 8;
		while (*p && idx < 11) {
			name83[idx++] = (*p >= 'a' && *p <= 'z')
			                    ? (char)(*p - 'a' + 'A')
			                    : *p;
			p++;
		}
	}

	uint16_t path_utf16[MAX_LONG_FILENAME] = {0};
	int utf16_len =
	    fat32_utf8_to_utf16(path, path_utf16, MAX_LONG_FILENAME);
	int lfn_count = (utf16_len + 12) / 13;
	int total_entries_needed = lfn_count + 1;

	uint32_t current_cluster = parent_fat->cluster;
	uint32_t last_cluster = current_cluster;
	uint32_t bytes_per_cluster =
	    parent_fat->sectors_per_cluster * parent_fat->bytes_per_sector;
	uint8_t* dir_buf = (uint8_t*)kalloc(bytes_per_cluster);

	int free_count = 0;
	uint32_t found_sector = 0;
	uint32_t found_offset = 0;

	while (current_cluster < 0x0FFFFFF8) {
		last_cluster = current_cluster;
		uint32_t sector =
		    fat32_cluster_to_sector(parent_fat, current_cluster);
		ops->read(block_vnode, sector, dir_buf, bytes_per_cluster);

		free_count = 0;
		for (uint32_t i = 0; i < bytes_per_cluster; i += 32) {
			struct fat32_dir_entry* entry =
			    (struct fat32_dir_entry*)(dir_buf + i);
			if (entry->name[0] == 0x00 ||
			    (uint8_t)entry->name[0] == 0xE5) {
				if (free_count == 0)
					found_offset = i;
				free_count++;
				if (free_count == total_entries_needed) {
					found_sector = sector;
					break;
				}
			} else {
				free_count = 0;
			}
		}
		if (free_count == total_entries_needed)
			break;
		current_cluster = fat32_get_next_cluster(
		    block_vnode, parent_fat, current_cluster);
	}

	/* Grow the directory if it is full */
	if (free_count != total_entries_needed) {
		uint32_t new_dir_cluster =
		    fat32_allocate_cluster(block_vnode, parent_fat);
		if (new_dir_cluster != 0 && new_dir_cluster < 0x0FFFFFF8) {
			// Link last_cluster -> new_dir_cluster in the FAT
			uint32_t fat_sector = parent_fat->fat_start_sector;
			uint32_t bytes_per_sector =
			    parent_fat->bytes_per_sector;
			uint32_t entries_per_sector = bytes_per_sector / 4;
			uint32_t* fat_buf = (uint32_t*)kalloc(bytes_per_sector);

			uint32_t last_clus_sector_offset =
			    (last_cluster * 4) / bytes_per_sector;
			uint32_t last_clus_entry_offset =
			    last_cluster % entries_per_sector;

			if (ops->read(block_vnode,
			              fat_sector + last_clus_sector_offset,
			              fat_buf, bytes_per_sector) >= 0) {
				fat_buf[last_clus_entry_offset] =
				    new_dir_cluster;
				for (uint32_t f = 0; f < parent_fat->fats_count;
				     f++) {
					ops->write(
					    block_vnode,
					    fat_sector +
					        last_clus_sector_offset +
					        (f *
					         parent_fat->size_in_sectors),
					    fat_buf, bytes_per_sector);
				}
			}
			kfree2(fat_buf);

			// Zero out the new cluster on disk
			uint8_t* zero_buf = (uint8_t*)kalloc(bytes_per_cluster);
			memset(zero_buf, 0, bytes_per_cluster);
			ops->write(block_vnode,
			           fat32_cluster_to_sector(parent_fat,
			                                   new_dir_cluster),
			           zero_buf, bytes_per_cluster);
			kfree2(zero_buf);

			found_sector = fat32_cluster_to_sector(parent_fat,
			                                       new_dir_cluster);
			found_offset = 0;
			free_count = total_entries_needed;

			memset(dir_buf, 0, bytes_per_cluster);
		}
	}

	/* Build LFN */
	if (free_count == total_entries_needed) {
		uint8_t chksum = fat32_sfn_checksum((uint8_t*)name83);
		int lfn_idx = lfn_count - 1;
		for (int i = 0; i < lfn_count; i++) {
			struct fat32_lfn_entry* lfn =
			    (struct fat32_lfn_entry*)(dir_buf + found_offset +
			                              i * 32);
			lfn->order =
			    (uint8_t)((lfn_idx + 1) | (i == 0 ? 0x40 : 0x00));
			lfn->attr = 0x0F;
			lfn->type = 0;
			lfn->checksum = chksum;
			lfn->zero = 0;

			int char_idx = lfn_idx * 13;
			uint16_t get_char = 0;
			for (int c = 0; c < 5; c++) {
				if (char_idx < utf16_len)
					get_char = path_utf16[char_idx];
				else if (char_idx == utf16_len)
					get_char = 0x0000;
				else
					get_char = 0xFFFF;
				lfn->name1[c] = get_char;
				char_idx++;
			}
			for (int c = 0; c < 6; c++) {
				if (char_idx < utf16_len)
					get_char = path_utf16[char_idx];
				else if (char_idx == utf16_len)
					get_char = 0x0000;
				else
					get_char = 0xFFFF;
				lfn->name2[c] = get_char;
				char_idx++;
			}
			for (int c = 0; c < 2; c++) {
				if (char_idx < utf16_len)
					get_char = path_utf16[char_idx];
				else if (char_idx == utf16_len)
					get_char = 0x0000;
				else
					get_char = 0xFFFF;
				lfn->name3[c] = get_char;
				char_idx++;
			}
			lfn_idx--;
		}

		struct fat32_dir_entry* dir =
		    (struct fat32_dir_entry*)(dir_buf + found_offset +
		                              lfn_count * 32);
		memcopy(dir->name, name83, 11);
		dir->attr = FAT_ATTR_ARCHIVE;
		dir->reserved = 0;
		dir->creation_time_tenths = 0;
		dir->creation_time = 0;
		dir->creation_date = 0;
		dir->access_date = 0;
		dir->modify_time = 0;
		dir->modify_date = 0;

		uint32_t new_cluster =
		    fat32_allocate_cluster(block_vnode, parent_fat);
		dir->cluster_high = (new_cluster >> 16) & 0xFFFF;
		dir->cluster_low = new_cluster & 0xFFFF;
		dir->size = 0;

		ops->write(block_vnode, found_sector, dir_buf,
		           bytes_per_cluster);

		uint8_t* zero_buf = (uint8_t*)kalloc(bytes_per_cluster);
		memset(zero_buf, 0, bytes_per_cluster);
		ops->write(block_vnode,
		           fat32_cluster_to_sector(parent_fat, new_cluster),
		           zero_buf, bytes_per_cluster);
		kfree2(zero_buf);

		int lookup_ret = fat32_lookup(instance, path, parent, out);
		if (lookup_ret < 0) {
			serial2_printf("fat32_create: fat32_lookup failed "
			               "after writing!\n");
		}

		return lookup_ret;
	}

	serial2_printf("fat32_create: could not find enough free entries "
	               "(needed %d, found %d)\n",
	               total_entries_needed, free_count);
	kfree2(dir_buf);
	return -1;
}

static void fat32_free_cluster_chain(vnode_t* block_vnode,
                                     struct fat32_internal_data* fat_node,
                                     uint32_t start_cluster) {
	if (start_cluster == 0 || start_cluster >= 0x0FFFFFF8)
		return;
	vops_blk_t* ops = (vops_blk_t*)block_vnode->ops;
	uint32_t fat_sector = fat_node->fat_start_sector;
	uint32_t bytes_per_sector = fat_node->bytes_per_sector;
	uint32_t* fat_buf = (uint32_t*)kalloc(bytes_per_sector);
	uint32_t current = start_cluster;
	uint32_t current_sector_idx = 0xFFFFFFFF;
	bool dirty = false;
	uint32_t freed_count = 0;

	while (current >= 2 && current < 0x0FFFFFF8) {
		uint32_t sector_idx = current / (bytes_per_sector / 4);
		uint32_t offset_in_sector = current % (bytes_per_sector / 4);
		if (current_sector_idx != sector_idx) {
			if (dirty && current_sector_idx != 0xFFFFFFFF) {
				for (uint32_t f = 0; f < fat_node->fats_count;
				     f++) {
					ops->write(
					    block_vnode,
					    fat_sector + current_sector_idx +
					        (f * fat_node->size_in_sectors),
					    fat_buf, bytes_per_sector);
				}
				dirty = false;
			}
			if (ops->read(block_vnode, fat_sector + sector_idx,
			              fat_buf, bytes_per_sector) < 0)
				break;
			current_sector_idx = sector_idx;
		}
		uint32_t next = fat_buf[offset_in_sector] & 0x0FFFFFFF;
		fat_buf[offset_in_sector] = 0x00000000;
		dirty = true;
		current = next;
		freed_count++;
	}
	if (dirty && current_sector_idx != 0xFFFFFFFF) {
		for (uint32_t f = 0; f < fat_node->fats_count; f++) {
			ops->write(block_vnode,
			           fat_sector + current_sector_idx +
			               (f * fat_node->size_in_sectors),
			           fat_buf, bytes_per_sector);
		}
	}
	if (freed_count > 0) {
		fat_node->fsinfo.free_cluster_count += freed_count;
		fat32_write_fsinfo(block_vnode, fat_node);
	}
	kfree2(fat_buf);
}

static int fat32_unlink(struct fs_instance* instance, char* path,
                        dentry_ptr parent) {
	auto block_vnode = instance->block_dentry->vnode;
	auto ops = (vops_blk_t*)block_vnode->ops;
	auto parent_fat =
	    (struct fat32_internal_data*)parent->vnode->vnode_private;
	uint32_t bytes_per_cluster =
	    parent_fat->sectors_per_cluster * parent_fat->bytes_per_sector;
	uint8_t* dir_buf = (uint8_t*)kalloc(bytes_per_cluster);
	uint32_t current_cluster = parent_fat->cluster;
	int found = 0;

	while (current_cluster < 0x0FFFFFF8) {
		uint32_t sector =
		    fat32_cluster_to_sector(parent_fat, current_cluster);
		if (ops->read(block_vnode, sector, dir_buf, bytes_per_cluster) <
		    0)
			break;
		uintptr_t base = (uintptr_t)dir_buf;
		uintptr_t ptr = base;
		uintptr_t end = base + bytes_per_cluster;
		uint16_t lfn_utf16[MAX_LONG_FILENAME] = {0};
		int lfn_active = 0;
		int lfn_start_offset = -1;

		while (ptr < end) {
			struct fat32_dir_entry* entry =
			    (struct fat32_dir_entry*)ptr;
			if (entry->name[0] == 0x00) {
				current_cluster = 0x0FFFFFFF;
				break;
			}
			if ((uint8_t)entry->name[0] == 0xE5) {
				lfn_active = 0;
				lfn_start_offset = -1;
				ptr += 32;
				continue;
			}
			if (entry->attr == FAT_ATTR_LFN) {
				if (lfn_start_offset == -1)
					lfn_start_offset = (int)(ptr - base);
				struct fat32_lfn_entry* lfn =
				    (struct fat32_lfn_entry*)ptr;
				int index = (lfn->order & 0x1F) - 1;
				if (index >= 0 && index < 20) {
					uint16_t* p_buf =
					    lfn_utf16 + index * 13;
					p_buf[0] = lfn->name1[0];
					p_buf[1] = lfn->name1[1];
					p_buf[2] = lfn->name1[2];
					p_buf[3] = lfn->name1[3];
					p_buf[4] = lfn->name1[4];
					p_buf[5] = lfn->name2[0];
					p_buf[6] = lfn->name2[1];
					p_buf[7] = lfn->name2[2];
					p_buf[8] = lfn->name2[3];
					p_buf[9] = lfn->name2[4];
					p_buf[10] = lfn->name2[5];
					p_buf[11] = lfn->name3[0];
					p_buf[12] = lfn->name3[1];
					lfn_active = 1;
				}
				ptr += 32;
				continue;
			}
			char name[256] = {0};
			char sfn_name[16];
			int has_lfn = lfn_active;
			if (has_lfn) {
				fat32_utf16_to_utf8(lfn_utf16, name, 256);
			}

			if (lfn_start_offset == -1)
				lfn_start_offset = (int)(ptr - base);
			int name_idx = 0;
			for (int i = 0; i < 8; i++) {
				if (entry->name[i] != ' ')
					sfn_name[name_idx++] = entry->name[i];
			}
			if (entry->name[8] != ' ') {
				sfn_name[name_idx++] = '.';
				for (int i = 8; i < 11; i++) {
					if (entry->name[i] != ' ')
						sfn_name[name_idx++] =
						    entry->name[i];
				}
			}
			sfn_name[name_idx] = '\0';

			to_lowercase(sfn_name);
			if (has_lfn)
				to_lowercase(name);

			int match = 0;
			if (has_lfn && strlen(name) == strlen(path) &&
			    strncmp(name, path, strlen(path)) == 0)
				match = 1;
			if (!match && strlen(sfn_name) == strlen(path) &&
			    strncmp(sfn_name, path, strlen(path)) == 0)
				match = 1;

			if (match) {
				if (entry->attr & FAT_ATTR_DIRECTORY) {
					kfree2(dir_buf);
					return -21; /* -EISDIR */
				}
				uint32_t file_cluster =
				    ((uint32_t)entry->cluster_high << 16) |
				    entry->cluster_low;
				int end_offset = (int)(ptr - base) + 32;
				for (int i = lfn_start_offset; i < end_offset;
				     i += 32)
					dir_buf[i] = 0xE5;
				ops->write(block_vnode, sector, dir_buf,
				           bytes_per_cluster);
				fat32_free_cluster_chain(
				    block_vnode, parent_fat, file_cluster);
				found = 1;
				break;
			}
			lfn_active = 0;
			memset(lfn_utf16, 0, sizeof(lfn_utf16));
			lfn_start_offset = -1;
			ptr += 32;
		}
		if (found)
			break;
		if (current_cluster < 0x0FFFFFF8)
			current_cluster = fat32_get_next_cluster(
			    block_vnode, parent_fat, current_cluster);
	}
	kfree2(dir_buf);
	if (found)
		return VFS_OK;
	return -1;
}

int fat32_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	if (!vnode || !buf || !len)
		return -1;

	auto fat_node = (struct fat32_internal_data*)vnode->vnode_private;
	if (!fat_node)
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

	if (offset >= fat_node->size)
		return 0;
	size_t read_len = len;
	if (offset + read_len > fat_node->size) {
		read_len = fat_node->size - offset;
	}

	uint32_t bytes_per_cluster =
	    fat_node->sectors_per_cluster * fat_node->bytes_per_sector;
	uint32_t current_cluster = fat_node->cluster;
	size_t current_offset = 0;

	while (offset >= current_offset + bytes_per_cluster) {
		current_cluster = fat32_get_next_cluster(block_vnode, fat_node,
		                                         current_cluster);
		current_offset += bytes_per_cluster;
		if (current_cluster >= 0x0FFFFFF8)
			return 0;
	}

	size_t byte_offset_in_cluster = offset - current_offset;
	size_t bytes_left = read_len;
	uint8_t* out_buf = (uint8_t*)buf;

	void* temp_buf = kalloc(bytes_per_cluster);
	if (!temp_buf)
		return -1;

	while (bytes_left > 0 && current_cluster < 0x0FFFFFF8) {
		uint32_t sector =
		    fat32_cluster_to_sector(fat_node, current_cluster);
		if (ops->read(block_vnode, sector, temp_buf,
		              bytes_per_cluster) < 0) {
			LOG2_ERROR("FAT32", "failed to read file cluster");
			break;
		}

		size_t to_read_from_cluster =
		    bytes_per_cluster - byte_offset_in_cluster;
		if (to_read_from_cluster > bytes_left) {
			to_read_from_cluster = bytes_left;
		}

		memcopy(out_buf,
		        (void*)((uintptr_t)temp_buf + byte_offset_in_cluster),
		        to_read_from_cluster);

		out_buf += to_read_from_cluster;
		bytes_left -= to_read_from_cluster;
		byte_offset_in_cluster = 0;

		if (bytes_left > 0) {
			current_cluster = fat32_get_next_cluster(
			    block_vnode, fat_node, current_cluster);
		}
	}

	kfree2(temp_buf);
	return (int)(read_len - bytes_left);
}

static void fat32_set_next_cluster(vnode_t* block_vnode,
                                   struct fat32_internal_data* info,
                                   uint32_t current_cluster,
                                   uint32_t next_cluster) {
	vops_blk_t* ops = (vops_blk_t*)block_vnode->ops;
	uint32_t fat_offset = current_cluster * 4;
	uint32_t fat_sector =
	    info->fat_start_sector + (fat_offset / info->bytes_per_sector);
	uint32_t ent_offset = fat_offset % info->bytes_per_sector;

	uint8_t* sec_buf = (uint8_t*)kalloc(info->bytes_per_sector);
	if (!sec_buf)
		return;

	if (ops->read(block_vnode, fat_sector, sec_buf,
	              info->bytes_per_sector) >= 0) {
		uint32_t old_val;
		memcopy(&old_val, (void*)((uintptr_t)sec_buf + ent_offset),
		        sizeof(uint32_t));
		next_cluster =
		    (old_val & 0xF0000000) | (next_cluster & 0x0FFFFFFF);
		memcopy(&sec_buf[ent_offset], &next_cluster, sizeof(uint32_t));
		for (uint32_t f = 0; f < info->fats_count; f++) {
			ops->write(block_vnode,
			           fat_sector + (f * info->size_in_sectors),
			           sec_buf, info->bytes_per_sector);
		}
	}
	kfree2(sec_buf);
}

long fat32_write(vnode_t* vnode, void* buf, size_t len, size_t offset) {
	if (!vnode || !buf || !len)
		return -1;

	auto fat_node = (struct fat32_internal_data*)vnode->vnode_private;
	if (!fat_node)
		return -2;

	auto block_dentry = vnode->fs_instance->block_dentry;
	if (!block_dentry)
		return -2;

	auto block_vnode = block_dentry->vnode;
	if (!block_vnode)
		return -2;

	auto ops = (vops_blk_t*)block_vnode->ops;
	if (!ops || !ops->write)
		return -3;

	uint32_t bytes_per_cluster =
	    fat_node->sectors_per_cluster * fat_node->bytes_per_sector;

	size_t write_len = len;
	bool file_extended = false;

	if (offset + write_len > fat_node->size) {
		fat_node->size = (uint32_t)(offset + write_len);
		if (vnode)
			vnode->size = fat_node->size;
		file_extended = true;
	}

	uint32_t current_cluster = fat_node->cluster;
	if (current_cluster == 0) {
		current_cluster = fat32_allocate_cluster(block_vnode, fat_node);
		if (current_cluster == 0)
			return 0;
		fat_node->cluster = current_cluster;
		file_extended = true;
	}
	serial2_printf("FAT32: current_cluster : %d\n", current_cluster);
	serial2_printf("FAT32: file_extended : %b\n", file_extended);

	size_t current_offset = 0;

	while (offset >= current_offset + bytes_per_cluster) {
		uint32_t next = fat32_get_next_cluster(block_vnode, fat_node,
		                                       current_cluster);
		if (next >= 0x0FFFFFF8 || next == 0) {
			next = fat32_allocate_cluster(block_vnode, fat_node);
			if (next == 0)
				break; // Out of space
			fat32_set_next_cluster(block_vnode, fat_node,
			                       current_cluster, next);
		}
		current_cluster = next;
		current_offset += bytes_per_cluster;
	}

	if (offset >= current_offset + bytes_per_cluster) {
		return 0; // Failed to reach offset
	}

	size_t byte_offset_in_cluster = offset - current_offset;
	size_t bytes_left = write_len;
	uint8_t* in_buf = (uint8_t*)buf;

	void* temp_buf = kalloc(bytes_per_cluster);
	if (!temp_buf)
		return -1;

	while (bytes_left > 0 && current_cluster < 0x0FFFFFF8) {
		uint32_t sector =
		    fat32_cluster_to_sector(fat_node, current_cluster);
		size_t needed_to_write =
		    bytes_per_cluster - byte_offset_in_cluster;
		if (needed_to_write > bytes_left)
			needed_to_write = bytes_left;

		if (needed_to_write < bytes_per_cluster) {
			if (ops->read(block_vnode, sector, temp_buf,
			              bytes_per_cluster) < 0) {
				LOG2_ERROR(
				    "FAT32",
				    "failed to read file cluster for rmw");
				break;
			}
		}

		memcopy((void*)((uintptr_t)temp_buf + byte_offset_in_cluster),
		        in_buf, needed_to_write);

		if (ops->write(block_vnode, sector, temp_buf,
		               bytes_per_cluster) < 0) {
			LOG2_ERROR("FAT32", "failed to write file cluster");
			break;
		}

		in_buf += needed_to_write;
		bytes_left -= needed_to_write;
		byte_offset_in_cluster = 0;

		if (bytes_left > 0) {
			uint32_t next = fat32_get_next_cluster(
			    block_vnode, fat_node, current_cluster);
			if (next >= 0x0FFFFFF8 || next == 0) {
				next = fat32_allocate_cluster(block_vnode,
				                              fat_node);
				if (next == 0)
					break; // Out of space
				fat32_set_next_cluster(block_vnode, fat_node,
				                       current_cluster, next);
			}
			current_cluster = next;
		}
	}

	if (file_extended && fat_node->dir_sector != 0) {
		uint8_t* dir_buf = (uint8_t*)kalloc(bytes_per_cluster);
		if (dir_buf) {
			if (ops->read(block_vnode, fat_node->dir_sector,
			              dir_buf, bytes_per_cluster) >= 0) {

				struct fat32_dir_entry* entry =
				    (struct fat32_dir_entry*)(dir_buf +
				                              fat_node
				                                  ->dir_offset);

				// MS FAT Spec Section 6.1: Update field
				// "DIR_FileSize" (offset 28, ukuran 4 byte).
				entry->size = fat_node->size;

				entry->cluster_high =
				    (fat_node->cluster >> 16) & 0xFFFF;
				entry->cluster_low = fat_node->cluster & 0xFFFF;

				ops->write(block_vnode, fat_node->dir_sector,
				           dir_buf, bytes_per_cluster);
			}
			kfree2(dir_buf);
		}
	}

	kfree2(temp_buf);
	return (long)(write_len - bytes_left);
}

static int fat32_flush(vnode_t* vnode) {
	if (!vnode || !vnode->fs_instance || !vnode->fs_instance->block_dentry)
		return -1;
	auto block_vnode = vnode->fs_instance->block_dentry->vnode;
	if (!block_vnode)
		return -1;
	auto block_ops = (vops_blk_t*)block_vnode->ops;
	if (block_ops && block_ops->flush) {
		return block_ops->flush(block_vnode);
	}
	return 0;
}

static int fat32_ioctl(vnode_t* vnode, uint32_t req, void* arg) {
	UNUSED(vnode);
	UNUSED(req);
	UNUSED(arg);
	return -1;
}

int fat32_readlink(vnode_t* vnode, char* buf, size_t len) {
	auto priv_data = (char*)vnode->vnode_private;
	serial2_printf("%s \n", (char*)priv_data);
	if (!priv_data)
		return -1;

	auto len_ = strlen(priv_data) > len ? len : strlen(priv_data);
	memcopy(buf, priv_data, len_);
	buf[strlen(priv_data)] = '\0';

	return (int)len_;
}

static int fat32_truncate(vnode_t* vnode, size_t length) {
	if (!vnode)
		return -1;
	auto fat_node = (struct fat32_internal_data*)vnode->vnode_private;
	if (!fat_node)
		return -1;

	// for now, only support truncate into 0
	if (length != 0)
		return -1;

	if (fat_node->size == 0)
		return 0;

	auto block_dentry = vnode->fs_instance->block_dentry;
	if (!block_dentry)
		return -1;
	auto block_vnode = block_dentry->vnode;
	if (!block_vnode)
		return -1;
	auto ops = (vops_blk_t*)block_vnode->ops;

	// Bebaskan seluruh rangkaian cluster yang dimiliki file ini
	fat32_free_cluster_chain(block_vnode, fat_node, fat_node->cluster);

	// Reset metadata memori
	fat_node->cluster = 0;
	fat_node->size = 0;
	vnode->size = 0;

	// Perbarui directory entry ke disk agar size dan cluster_high/low
	// menjadi 0
	if (fat_node->dir_sector != 0) {
		uint32_t bytes_per_cluster =
		    fat_node->sectors_per_cluster * fat_node->bytes_per_sector;
		uint8_t* dir_buf = (uint8_t*)kalloc(bytes_per_cluster);
		if (dir_buf) {
			if (ops->read(block_vnode, fat_node->dir_sector,
			              dir_buf, bytes_per_cluster) >= 0) {
				struct fat32_dir_entry* entry =
				    (struct fat32_dir_entry*)(dir_buf +
				                              fat_node
				                                  ->dir_offset);
				entry->size = 0;
				entry->cluster_high = 0;
				entry->cluster_low = 0;
				ops->write(block_vnode, fat_node->dir_sector,
				           dir_buf, bytes_per_cluster);
			}
			kfree2(dir_buf);
		}
	}

	return 0;
}

static int fat32_readdir(vnode_t* vnode, void* buf, size_t len, uint64_t* pos) {
	if (!vnode || !buf || !len)
		return -1;

	auto dev_vnode = vnode->fs_instance->block_dentry->vnode;

	auto priv_data = vnode->vnode_private;
	if (!priv_data)
		return -1;

	auto fat_node = (struct fat32_internal_data*)priv_data;
	if (!fat_node)
		return -1;

	auto ops = (vops_blk_t*)dev_vnode->ops;
	if (!ops)
		return -1;

	uint32_t bytes_per_cluster =
	    fat_node->sectors_per_cluster * fat_node->bytes_per_sector;
	uint8_t* dir_buf = (uint8_t*)kalloc(bytes_per_cluster);
	if (!dir_buf) {
		LOG2_ERROR("FAT32", "failed to alloc dir buffer");
		return -2;
	}

	int bytes_written = 0;
	int index = 0;

	auto current_cluster = fat_node->cluster;
	while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8) {
		uint32_t sector =
		    fat32_cluster_to_sector(fat_node, current_cluster);
		if (ops->read(dev_vnode, sector, dir_buf, bytes_per_cluster) <
		    0) {
			LOG2_ERROR("FAT32", "failed to read dir cluster");
			break;
		}

		uintptr_t base = (uintptr_t)dir_buf;
		uintptr_t ptr = base;
		uintptr_t end = base + bytes_per_cluster;

		uint16_t lfn_utf16[MAX_LONG_FILENAME] = {0};
		int lfn_active = 0;

		boolean_t finished = false;

		while (ptr < end) {
			struct fat32_dir_entry* entry =
			    (struct fat32_dir_entry*)ptr;

			if (entry->name[0] == 0x00) {
				current_cluster = 0x0FFFFFFF;
				break;
			}

			if ((uint8_t)entry->name[0] == 0xE5) {
				ptr += 32;
				continue;
			}

			if (entry->attr == FAT_ATTR_LFN) {
				struct fat32_lfn_entry* lfn =
				    (struct fat32_lfn_entry*)ptr;
				int idx = (lfn->order & 0x1F) - 1;
				if (idx >= 0 && idx < 20) {
					uint16_t* p = lfn_utf16 + idx * 13;
					p[0] = lfn->name1[0];
					p[1] = lfn->name1[1];
					p[2] = lfn->name1[2];
					p[3] = lfn->name1[3];
					p[4] = lfn->name1[4];
					p[5] = lfn->name2[0];
					p[6] = lfn->name2[1];
					p[7] = lfn->name2[2];
					p[8] = lfn->name2[3];
					p[9] = lfn->name2[4];
					p[10] = lfn->name2[5];
					p[11] = lfn->name3[0];
					p[12] = lfn->name3[1];
					lfn_active = 1;
				}
				ptr += 32;
				continue;
			}

			char name[256] = {0};
			char sfn_name[16];
			int has_lfn = lfn_active;
			if (has_lfn) {
				lfn_active = 0;
				fat32_utf16_to_utf8(lfn_utf16, name, 256);
				memset(lfn_utf16, 0, sizeof(lfn_utf16));
			}

			int name_idx = 0;
			for (int i = 0; i < 8; i++) {
				if (entry->name[i] != ' ') {
					sfn_name[name_idx++] = entry->name[i];
				}
			}
			if (entry->name[8] != ' ') {
				sfn_name[name_idx++] = '.';
				for (int i = 8; i < 11; i++) {
					if (entry->name[i] != ' ') {
						sfn_name[name_idx++] =
						    entry->name[i];
					}
				}
			}
			sfn_name[name_idx] = '\0';

			to_lowercase(sfn_name);
			if (has_lfn)
				to_lowercase(name);

			const char* final_name = has_lfn ? name : sfn_name;
			size_t name_len = strlen(final_name);

			// Hitung reclen (offset d_name + nama + null
			// terminator) dan sejajarkan (align) ke 8-byte
			size_t reclen =
			    offsetof(struct dirent, d_name) + name_len + 1;
			reclen = (reclen + 7) & ~7UL;

			if ((uint64_t)index >= *pos) {
				// Jika buffer user penuh
				if (bytes_written + (int)reclen > (int)len) {
					finished = true;
					break;
				}

				struct dirent* de =
				    (struct dirent*)(void*)((char*)buf +
				                            bytes_written);
				de->d_ino =
				    (uint64_t)(((uint32_t)entry->cluster_high
				                << 16) |
				               entry->cluster_low);
				if (de->d_ino == 0)
					de->d_ino =
					    2; // root directory fallback
				de->d_off = (uint64_t)(index + 1);
				de->d_reclen = (uint16_t)reclen;
				de->d_type = (entry->attr & FAT_ATTR_DIRECTORY)
				                 ? 4
				                 : 8; // DT_DIR=4, DT_REG=8

				strncpy(de->d_name, final_name, name_len);
				de->d_name[name_len] = '\0';

				bytes_written += reclen;
				*pos = (uint64_t)(index + 1);
			}
			
			index++;
			ptr += 32;
		}

		if (finished)
			break;

		current_cluster = fat32_get_next_cluster(dev_vnode, fat_node,
		                                         current_cluster);
	}

	kfree2(dir_buf);
	return bytes_written; // Mengembalikan jumlah byte yang telah diformat
}

vops_file_t* fat32_file_operations(void) {
	_file_ops.read = fat32_read;
	_file_ops.write = fat32_write;
	_file_ops.ioctl = fat32_ioctl;
	_file_ops.flush = fat32_flush;
	_file_ops.truncate = fat32_truncate;
	_file_ops.readdir = fat32_readdir;

	return &_file_ops;
}

static int fat32_mount(struct fs_instance* instance) {
	auto block_dentry = instance->block_dentry;
	if (!block_dentry)
		return -1;
	auto block_vnode = block_dentry->vnode;
	if (!block_vnode)
		return -1;
	auto ops = (vops_blk_t*)block_vnode->ops;
	if (!ops || !ops->read || !ops->write)
		return -1;

	auto bpb = (struct fat32_bpb*)kalloc(512);
	if (!bpb)
		return -2;

	if (ops->read(block_vnode, 0, bpb, 512) >= 0) {
		bpb->flags = 0x01; // Mark as dirty
		ops->write(block_vnode, 0, bpb, 512);
	}
	kfree2(bpb);
	return 0;
}

static int fat32_umount(struct fs_instance* instance) {
	auto block_dentry = instance->block_dentry;
	if (!block_dentry)
		return -1;
	auto block_vnode = block_dentry->vnode;
	if (!block_vnode)
		return -1;
	auto ops = (vops_blk_t*)block_vnode->ops;
	if (!ops || !ops->read || !ops->write)
		return -1;

	auto bpb = (struct fat32_bpb*)kalloc(512);
	if (!bpb)
		return -2;

	if (ops->read(block_vnode, 0, bpb, 512) >= 0) {
		bpb->flags = 0x00; // Mark as clean
		ops->write(block_vnode, 0, bpb, 512);
	}
	kfree2(bpb);
	return 0;
}

fs_operations_t* fat32_fs_operations(void) {
	_fs_ops.lookup = fat32_lookup;
	_fs_ops.create = fat32_create;
	_fs_ops.unlink = fat32_unlink;
	_fs_ops.mount = fat32_mount;
	_fs_ops.umount = fat32_umount;
	return &_fs_ops;
}

vops_lnk_t* fat32_lnk_operations(void) {
	_lnk_ops.readlink = fat32_readlink;
	return &_lnk_ops;
}
