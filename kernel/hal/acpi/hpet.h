#ifndef __HAL__ACPI__HPET_H__
#define __HAL__ACPI__HPET_H__

#include <libk/type.h>

#define HPET_GENERAL_CAP_ID 0x0
#define HPET_GENERAL_CONFIG 0x10
#define HPET_ENABLE_CNF 0x1
#define HPET_MAIN_COUNT 0x0F0
#define HPET_TIMER_CONFIG(N) 0x100 + N * 0x20
#define HPET_TIMER_COMPARATOR(N) 0x108 + N * 0x20
#define ms2ns(x) (x) * 1000000

struct address_structure
{
    uint8_t  address_space_id; // 0 - system memory, 1 - system I/O
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  reserved;
    uint64_t address;
} __attribute__((packed));

struct description_table_header
{
    char     signature[4]; // 'HPET' in case of HPET table
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    uint64_t oem_tableid;
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct hpet
{
    struct description_table_header header;
    uint8_t                         hardware_rev_id;
    uint8_t                         comparator_count : 5;
    uint8_t                         counter_size : 1;
    uint8_t                         reserved : 1;
    uint8_t                         legacy_replacement : 1;
    uint16_t                        pci_vendor_id;
    struct address_structure        address;
    uint8_t                         hpet_number;
    uint16_t                        minimum_tick;
    uint8_t                         page_protection;
} __attribute__((packed));

void     hpet_initialize(uintptr_t addr);
void     hpet_write(uint32_t reg, uint64_t value);
uint64_t hpet_read(uint32_t reg);
void     hpet_level_timer_setup(int n, uint64_t tick_count, int irq);
uint64_t hpet_min_tick_ns(void);
void     hpet_enable();
void     hpet_disable();

#endif // __HAL__ACPI__HPET_H__