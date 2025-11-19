#ifndef __HAL__PCI__PCI_H_
#define __HAL__PCI__PCI_H_

#include <libk/type.h>

#define MAX_PCI_BUS 256

enum PCI_HEADER_TYPE
{
    PCI_HEADER_TYPE_STANDARD_DEVICE   = 0,
    PCI_HEADER_TYPE_PCI_TO_PCI_BRIDGE = 1,
    PCI_HEADER_TYPE_CARDBUS_BRIDGE    = 2,
};

void     pci_scan();
uint16_t pci_readw(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
void     pci_writel(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);
uint8_t  pci_readb(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
uint32_t pci_readl(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);

#endif // __HAL__PCI__PCI_H_
