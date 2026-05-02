#ifndef __HAL__PCI__PCI_H_
#define __HAL__PCI__PCI_H_

#include <libk/type.h>

#define MAX_PCI_BUS 256

typedef struct pci_access_ops {
	uint32_t (*read32)(uintptr_t base, uint8_t bus, uint8_t dev,
			   uint8_t func, uint16_t offset);
	void (*write32)(uintptr_t base, uint8_t bus, uint8_t dev, uint8_t func,
			uint16_t offset, uint32_t val);
} pci_access_ops_t;

#define PCI_MAX_SEGMENTS 16

typedef enum : uint8_t {
	PCI_SEGMENT_LEGACY = 0,
	PCI_SEGMENT_PCIE = 1,
} PCI_SEGMENT_TYPE;

typedef struct pci_segment {
	uint16_t segment_id;
	uint8_t bus_start;
	uint8_t bus_end;
	uintptr_t vbase; /* virtual base setelah di-mmap */
	pci_access_ops_t* ops;
	boolean_t valid;
	PCI_SEGMENT_TYPE type;
} pci_segment_t;

enum PCI_HEADER_TYPE {
	PCI_HEADER_TYPE_STANDARD_DEVICE = 0,
	PCI_HEADER_TYPE_PCI_TO_PCI_BRIDGE = 1,
	PCI_HEADER_TYPE_CARDBUS_BRIDGE = 2,
};

void pci_scan();
// uint32_t pci_readl(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
// void pci_writel(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset,
// 		uint32_t value);

// uint16_t pci_readw(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
// uint8_t pci_readb(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
// void pci_writew(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset,
// 		uint16_t value);

#endif // __HAL__PCI__PCI_H_
