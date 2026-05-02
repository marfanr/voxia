#include "./pci.h"
#include <hal/graphic/virtio.h>
#include <ioforge/ioforge.h>
#include <ioforge/ioforge_pci.h>
#include <libk/io.h>
#include <libk/serial.h>

#define PCI_COMMAND 0XCF8
#define PCI_DATA 0xCFC

uint32_t pci_readl(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
	uint32_t address;
	uint32_t tmp = 0;
	address = (uint32_t) (((uint32_t) bus << 16) | ((uint32_t) device << 11)
			      | ((uint32_t) func << 8) | (offset & 0xFC)
			      | ((uint32_t) 0x80000000));
	outl(PCI_COMMAND, address);
	tmp = inl(PCI_DATA);
	return tmp;
}

void pci_writel(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset,
		uint32_t value) {
	uint32_t address;
	address = (uint32_t) (((uint32_t) bus << 16) | ((uint32_t) device << 11)
			      | ((uint32_t) func << 8) | (offset & 0xFC)
			      | ((uint32_t) 0x80000000));
	outl(PCI_COMMAND, address);
	outl(PCI_DATA, value);
}

uint32_t legacy_read32(uintptr_t base, uint8_t bus, uint8_t dev, uint8_t func,
		       uint16_t offset) {
	(void) base;
	return pci_readl(bus, dev, func, offset); /* existing impl */
}

void legacy_write32(uintptr_t base, uint8_t bus, uint8_t dev, uint8_t func,
		    uint16_t offset, uint32_t val) {
	(void) base;
	pci_writel(bus, dev, func, offset, val);
}

// static struct buddy_allocator *pci_allocator;

// uint16_t pci_readw(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
// 	uint32_t address;
// 	uint16_t tmp = 0;
// 	address = (uint32_t) (((uint32_t) bus << 16) | ((uint32_t) device << 11)
// 			      | ((uint32_t) func << 8) | (offset & 0xFC)
// 			      | ((uint32_t) 0x80000000));

// 	outl(PCI_COMMAND, address);
// 	tmp = (uint16_t) ((inl(PCI_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
// 	return tmp;
// }
// uint8_t pci_readb(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
// 	uint32_t address = (0x80000000U | (bus << 16) | (device << 11)
// 				| (func << 8) | (offset & 0xFC));
// 	outl(PCI_COMMAND, address);
// 	uint32_t data = inl(PCI_DATA);
// 	return (uint8_t) ((data >> ((offset & 3) * 8)) & 0xFF);
// }
// void pci_writew(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset,
// 		uint16_t value) {
// 	uint32_t address = 0x80000000 | (bus << 16) | (device << 11)
// 			   | (func << 8) | (offset & 0xFC);
// 	outl(0xCF8, address);
// 	if (offset & 2)
// 		outw(0xCFC + 2, value); // upper 16-bit
// 	else
// 		outw(0xCFC, value); // lower 16-bit
// }

// void pci_write_command(uint8_t bus, uint8_t dev, uint8_t func, uint16_t value) {
// 	uint32_t address =
// 		0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 0x04;

// 	outl(0xCF8, address);

// 	uint32_t data = inl(0xCFC);
// 	data = (data & 0xFFFF0000) | value;
// 	outl(0xCFC, data);
// }
