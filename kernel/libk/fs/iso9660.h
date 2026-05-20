#ifndef __FS__ISO9660_H__
#define __FS__ISO9660_H__

#include "vfs/filesystem.h"
#include <type.h>

typedef struct {
	uint32_t le; // little endian
	uint32_t be; // big endian
} both_u32;

typedef struct {
	uint16_t le;
	uint16_t be;
} both_u16;

// Struktur Primary Volume Descriptor (PVD)
struct iso9660_pvd {
	uint8_t type;	 // harus = 1 untuk PVD
	char id[5];	 // "CD001"
	uint8_t version; // harus = 1
	uint8_t unused1;

	char system_id[32]; // strA
	char volume_id[32]; // strD

	uint8_t unused2[8];

	both_u32 volume_space_size; // jumlah logical blocks

	uint8_t unused3[32];

	both_u16 volume_set_size;
	both_u16 volume_sequence_number;
	both_u16 logical_block_size;

	both_u32 path_table_size;

	uint32_t l_path_table_loc; // LBA lokasi type-L path table
	uint32_t opt_l_path_table_loc;
	uint32_t m_path_table_loc; // LBA lokasi type-M path table
	uint32_t opt_m_path_table_loc;

	// Root directory record
	// Struktur ini sebenarnya “Directory Record” dengan panjang tetap 34 byte pada PVD
	uint8_t root_dir_record[34];

	char volume_set_id[128];
	char publisher_id[128];
	char data_preparer_id[128];
	char application_id[128];
	char copyright_file_id[37];
	char abstract_file_id[37];
	char bibliographic_file_id[37];

	// Tanggal/waktu (dec-datetime), 17 byte tiap field
	char creation_date[17];
	char modification_date[17];
	char expiration_date[17];
	char effective_date[17];

	uint8_t file_structure_version;
	uint8_t unused4;

	uint8_t application_data[512];
	uint8_t reserved[653];
} __attribute__((packed));

// Struktur Path Table Entry
struct iso9660_path_table_entry {
	uint8_t dir_id_len;
	uint8_t ext_attr_rec_len;
	uint32_t extent_location;   // LBA, little-endian di L-Table
	uint16_t parent_dir_number; // indeks parent directory dalam path table
				    // lalu: char dir_identifier[dir_id_len];
	// lalu padding byte jika diperlukan agar entry genap
};

// Struktur Date/Time pada Directory Record
struct iso9660_dir_time {
	uint8_t year; // tahun sejak 1900
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	int8_t gmt_offset; // offset dari GMT dalam interval 15 menit
};

// Struktur Directory Record (Entry direktori / file)
struct iso9660_dir {
    uint8_t length;
    uint8_t ext_attr_length;

    uint32_t extent_le;
    uint32_t extent_be;

    uint32_t size_le;
    uint32_t size_be;

    uint8_t date[7];

    uint8_t flags;

    uint8_t file_unit_size;
    uint8_t interleave_gap;

    uint16_t volume_seq_le;
    uint16_t volume_seq_be;

    uint8_t name_len;

    char name[];
} __attribute__((packed));

struct iso9660_node {
	uint32_t extent;
	uint32_t size;
    uint8_t  flags; 
};

#define iOS9660_DIR_FLAG (1 << 1)

int iso9660_lookup(struct fs_instance* instance, char* path, dentry_ptr parent, dentry_ptr *out);
fs_operations_t* iso9660_file_operations(void);

#endif // __FS__ISO9660_H__