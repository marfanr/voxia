#ifndef __HAL__PCI__PCI_H_
#define __HAL__PCI__PCI_H_

#include <libk/type.h>

#define MAX_PCI_BUS 256

enum PCI_HEADER_TYPE
{
    PCI_HEADER_TYPE_STANDARD_DEVICE = 0,
    PCI_HEADER_TYPE_PCI_TO_PCI_BRIDGE = 1,
    PCI_HEADER_TYPE_CARDBUS_BRIDGE = 2,
};

void pci_scan ();

#endif // __HAL__PCI__PCI_H_