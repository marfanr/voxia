#ifndef __SYS__IOFORGE__IOFORGE_H_
#define __SYS__IOFORGE__IOFORGE_H_

#include <libk/type.h>

// ioforge /pci/00:01:03

enum IoForgeType
{
    IOFORGE_PCI = 1,
};

struct IoForgeService
{
    const char name[255];
    uint8_t    type;
    void (*init)(struct IoForgeService *);
    struct IoForgeService *next;
};

struct IoForgeACPI
{
    struct IoForgeService service;
};

void               ioforge_register_service(struct IoForgeService *service);
struct IoForgePCI *ioforge_get_pci_device(uint16_t vendor_id, uint16_t device_id);

#endif // __SYS__IOFORGE__IOFORGE_H_