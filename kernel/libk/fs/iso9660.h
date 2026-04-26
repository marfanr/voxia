#ifndef __FS__ISO9660_H__
#define __FS__ISO9660_H__

#include "type.h"

typedef struct
{
    uint32_t le; // little endian
    uint32_t be; // big endian
} both_u32;

typedef struct
{
    uint16_t le;
    uint16_t be;
} both_u16;

// Struktur Primary Volume Descriptor (PVD)
struct iso9660_pvd
{
    uint8_t type;    // harus = 1 untuk PVD
    char    id[5];   // "CD001"
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
};

// Struktur Path Table Entry
struct iso9660_path_table_entry
{
    uint8_t  dir_id_len;
    uint8_t  ext_attr_rec_len;
    uint32_t extent_location;   // LBA, little-endian di L-Table
    uint16_t parent_dir_number; // indeks parent directory dalam path table
    // lalu: char dir_identifier[dir_id_len];
    // lalu padding byte jika diperlukan agar entry genap
};

// Struktur Date/Time pada Directory Record
struct iso9660_dir_time
{
    uint8_t year; // tahun sejak 1900
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    int8_t  gmt_offset; // offset dari GMT dalam interval 15 menit
};

// Struktur Directory Record (Entry direktori / file)
struct iso9660_dir_record
{
    uint8_t                 length_dr;       // panjang record
    uint8_t                 ext_attr_length; // panjang atribut ekstensi
    both_u32                extent_location; // LBA dari extent (both-endian)
    both_u32                data_length;     // ukuran data (both-endian)
    struct iso9660_dir_time dt;              // date-time record
    uint8_t                 file_flags;
    uint8_t                 file_unit_size;
    uint8_t                 interleave_gap_size;
    both_u16                volume_sequence_number;
    uint8_t                 file_id_len;
    char                    file_id[]; // panjang variabel, diakhiri dengan ';' versi
    // setelah itu mungkin padding agar struktur genap
    // lalu System Use Area (SUA), panjang bisa sampai max record - fixed fields
};

#endif // __FS__ISO9660_H__