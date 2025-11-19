#ifndef __SYS__IOFORGE__IOFORGE_PCI_H_
#define __SYS__IOFORGE__IOFORGE_PCI_H_

#include "./ioforge.h"

enum IoForgeUSB_VERSION
{
    IoForgeUSB_VERSION_2 = 2
};

struct IoForgeUSB
{
    struct ioforge_service service;
    uint8_t                version;
    uint8_t                addr;
    uint8_t                endpoint;
    uint8_t                length;
    void                  *data;
    uint8_t                interrupt;
};

typedef struct
{
    void (*send)(uint32_t data_phys, size_t size);
} UsbControllerOp;

typedef struct
{
    const char      *name;
    UsbControllerOp *ops;
} USBController;

typedef struct
{
    uint8_t port;
    uint8_t vendor_id;
    uint8_t device_id;

} USBDevice;

#endif // __SYS__IOFORGE__IOFORGE_PCI_H_