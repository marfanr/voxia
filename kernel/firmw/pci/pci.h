#ifndef __FIRMW__PCI_H__
#define __FIRMW__PCI_H__

#include <libk/type.h>

typedef struct
{
    int bus;
    int slot;
    int func;
    uint16_t vendorID;
    uint16_t deviceID;
    uint16_t command;
    uint16_t status;
    uint8_t rev_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class;
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
    uint32_t bar[6];
    uint32_t cardbus_cis_pointer;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t expansion_rom_base_address;
    uint8_t capabilities_pointer;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint8_t min_grant;
    uint8_t max_latency;
} __attribute__ ((packed)) pci_device_t;

int pci_setup ();
uint16_t pciConfigReadWord (uint8_t bus, uint8_t slot, uint8_t func,
                            uint8_t offset);
void enable_bus_mastering (pci_device_t device);

int getInterruptStatus (pci_device_t device);
void pci_change_irq (pci_device_t device, uint8_t irq);
void enable_io_space (pci_device_t device);

extern pci_device_t pci_devices[32];

#endif // __FIRMW__PCI_H__
