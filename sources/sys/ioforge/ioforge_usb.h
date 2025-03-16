#ifndef __SYS__IOFORGE__IOFORGE_PCI_H_
#define __SYS__IOFORGE__IOFORGE_PCI_H_

#include "./ioforge.h"

enum IoForgeUSB_VERSION
{
    IoForgeUSB_VERSION_2 = 2
};

struct IoForgeUSB
{
    struct IoForgeService service;
    uint8_t version;
    uint8_t addr;
    uint8_t endpoint;
    uint8_t length;
    void *data;
    uint8_t interrupt;
};

#endif // __SYS__IOFORGE__IOFORGE_PCI_H_