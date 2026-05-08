#ifndef __SYS__IOFORGE__IOFORGE_PCI_H_
#define __SYS__IOFORGE__IOFORGE_PCI_H_

#include "./ioforge.h"

struct ioforge_pci_bar {
	uint64_t address;
	boolean_t iospace;
};

struct ioforge_pci_device {
	struct ioforge_device base;
	size_t pci_dev;
	size_t pci_bus;
	size_t pci_func;
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t command;
	uint16_t status;
	uint8_t subclass;
	uint8_t classes;
	uint8_t prog_if;
	uint8_t revision_id;
	uint8_t header_type;
	uint8_t interrupt_line;
	uint8_t interrupt_pin;
	uint8_t min_grant;
	uint8_t max_latency;
	uint8_t bus;
	uint8_t device;
	uint8_t function;
	uint16_t capability_ptr;
	struct ioforge_pci_bar bar[6];
};

#ifdef __cplusplus
extern "C" {
#endif

struct ioforge_pci_device*
ioforge_get_pci_device(uint16_t vendor_id, uint16_t device_id);

void pci_enable_msi(struct ioforge_pci_device* pci, uint8_t vector, uint8_t cpu,
		    uint16_t cap);
uint16_t pci_cap_find_msi(struct ioforge_pci_device* pci);
uint16_t pci_cap_find_msix(struct ioforge_pci_device* pci);
uintptr_t pci_enable_msix(struct ioforge_pci_device* pci, uint8_t vector,
			  uint8_t cpu, uint8_t cap);

#ifdef __cplusplus
}
#endif

#endif // __SYS__IOFORGE__IOFORGE_PCI_H_
