#ifndef __FS__FAT32_H__
#define __FS__FAT32_H__

#include "vfs/filesystem.h"
#include "vfs/vnode.h"
#include <type.h>

// MS FAT Spec Section 3.2 (Pages 9-13): BPB and FAT32 Extended Boot Record
struct fat32_bpb {
	uint8_t jmp[3];
	char oem_id[8];
	uint16_t bytes_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t fats_count;
	uint16_t root_dir_entries;
	uint16_t total_sectors_16;
	uint8_t media_descriptor;
	uint16_t sectors_per_fat_16;
	uint16_t sectors_per_track;
	uint16_t heads_count;
	uint32_t hidden_sectors;
	uint32_t total_sectors_32;

	uint32_t sectors_per_fat_32;
	uint16_t fat_flags;
	uint16_t version;
	uint32_t root_cluster;
	uint16_t fsinfo_sector;
	uint16_t backup_boot_sector;
	uint8_t reserved[12];
	uint8_t drive_number;
	uint8_t flags;
	uint8_t signature;
	uint32_t volume_id;
	char volume_label[11];
	char system_id[8];
	uint8_t boot_code[420];
	uint16_t boot_signature;
} __attribute__((packed));

// MS FAT Spec Section 6.1 (Page 22): FAT Directory Structure (Short Entry)
struct fat32_dir_entry {
	char name[11];
	uint8_t attr;
	uint8_t reserved;
	uint8_t creation_time_tenths;
	uint16_t creation_time;
	uint16_t creation_date;
	uint16_t access_date;
	uint16_t cluster_high;
	uint16_t modify_time;
	uint16_t modify_date;
	uint16_t cluster_low;
	uint32_t size;
} __attribute__((packed));

// FSInfo (Page 21)
struct fat32_fsinfo {
	uint32_t lead_signature;
	uint8_t reserved1[480];
	uint32_t struct_signature;
	uint32_t free_cluster_count;
	uint32_t next_free_cluster;
	uint8_t reserved2[12];
	uint32_t trail_signature;
} __attribute__((packed));

// MS FAT Spec Section 7.1 (Page 26): Long Directory Entry Structure
struct fat32_lfn_entry {
	uint8_t order;
	uint16_t name1[5];
	uint8_t attr;
	uint8_t type;
	uint8_t checksum;
	uint16_t name2[6];
	uint16_t zero;
	uint16_t name3[2];
} __attribute__((packed));

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN 0x02
#define FAT_ATTR_SYSTEM 0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE 0x20
#define FAT_ATTR_LFN (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

#define FAT32_LEAD_SIG 0x41615252
#define FAT32_STRUCT_SIG 0x61417272
#define FAT32_TRAIL_SIG 0xAA550000

fs_operations_t* fat32_fs_operations(void);
vops_file_t* fat32_file_operations(void);
vops_lnk_t* fat32_lnk_operations(void);

int fat32_lookup(struct fs_instance* instance, char* path, dentry_ptr parent,
                   dentry_ptr* out);
int fat32_read(vnode_t* vnode, void* buf, size_t len, size_t offset);
long fat32_write(vnode_t* vnode, void* buf, size_t len, size_t offset);
int fat32_readlink(vnode_t* vnode, char* buf, size_t len);

#endif // __FS__FAT32_H__