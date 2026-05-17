#ifndef __HAL__PCI__PCI_H_
#define __HAL__PCI__PCI_H_

#include <type.h>

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

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off);
uint64_t pci_read64(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off);
uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off);
void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off,
		 uint32_t val);
void pci_write64(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off,
		 uint64_t val);
void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off,
		 uint16_t val);
void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off,
		uint8_t val);

uint32_t legacy_read32(uintptr_t base, uint8_t bus, uint8_t dev, uint8_t func,
		       uint16_t offset);
void legacy_write32(uintptr_t base, uint8_t bus, uint8_t dev, uint8_t func,
		    uint16_t offset, uint32_t val);

void register_segment(uint16_t seg_id, uint8_t start, uint8_t end,
		      uintptr_t vbase, pci_access_ops_t* ops,
		      PCI_SEGMENT_TYPE type);
#endif // __HAL__PCI__PCI_H_
