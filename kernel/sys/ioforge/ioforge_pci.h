#ifndef __SYS__IOFORGE__IOFORGE_PCI_H_
#define __SYS__IOFORGE__IOFORGE_PCI_H_

#include "./ioforge.h"

struct IoForgePCIBar {
    uint32_t address;
    boolean_t     iospace;
};

struct IoForgePCI {
    struct IoForgeService service;
    size_t                pci_dev;
    size_t                pci_bus;
    size_t                pci_func;
    uint16_t              vendor_id;
    uint16_t              device_id;
    uint16_t              command;
    uint16_t              status;
    uint8_t               subclass;
    uint8_t class;
    uint8_t              prog_if;
    uint8_t              revision_id;
    uint8_t              header_type;
    uint8_t              interrupt_line;
    uint8_t              interrupt_pin;
    uint8_t              min_grant;
    uint8_t              max_latency;
    uint8_t              bus;
    uint8_t              device;
    uint8_t              function;
    uint8_t              capability_ptr;
    struct IoForgePCIBar bar[5];
};

#endif // __SYS__IOFORGE__IOFORGE_PCI_H_
