#ifndef __SYS__IOFORGE__IOFORGE_PCI_H_
#define __SYS__IOFORGE__IOFORGE_PCI_H_

#include "./ioforge.h"

struct ioforge_pci_bar
{
    uint64_t  address;
    boolean_t iospace;
};

struct ioforge_pci_service
{
    struct ioforge_service service;
    size_t                 pci_dev;
    size_t                 pci_bus;
    size_t                 pci_func;
    uint16_t               vendor_id;
    uint16_t               device_id;
    uint16_t               command;
    uint16_t               status;
    uint8_t                subclass;
    uint8_t                classes;
    uint8_t                prog_if;
    uint8_t                revision_id;
    uint8_t                header_type;
    uint8_t                interrupt_line;
    uint8_t                interrupt_pin;
    uint8_t                min_grant;
    uint8_t                max_latency;
    uint8_t                bus;
    uint8_t                device;
    uint8_t                function;
    uint16_t               capability_ptr;
    struct ioforge_pci_bar bar[6];
};

#ifdef __cplusplus
extern "C"
{
#endif
    struct ioforge_pci_service *ioforge_get_pci_device(uint16_t vendor_id, uint16_t device_id);

#ifdef __cplusplus
}
#endif

#endif // __SYS__IOFORGE__IOFORGE_PCI_H_
