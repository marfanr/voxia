#ifndef __HAL__PCI__PCIE_H__
#define __HAL__PCI__PCIE_H__

#include <libk/type.h>

typedef struct
{
    uint32_t signature;
    uint32_t length;
    uint8_t  revision;
    uint8_t  cheksum;
    uint8_t  oem_id[6];
    uint8_t  oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
    uint64_t reserved;

} MCFG_t;

void mcfg_parse(uintptr_t addr);

#endif // __HAL__PCI__PCIE_H__