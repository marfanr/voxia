#ifndef __HAL__PCI__PCIE_H__
#define __HAL__PCI__PCIE_H__

#include <hal/acpi/acpi.h>
#include <libk/type.h>

typedef struct MCFG_configuration_space_t {
	uint64_t base_addr;
	uint16_t pci_segment_group;
	uint8_t start_pci_bus;
	uint8_t end_pci_bus;
	uint32_t reserved;
} __attribute__((packed)) MCFG_configuration_space_t;

typedef struct {
	struct SDT sdt;
	uint64_t reserved;
	MCFG_configuration_space_t conf[];
} __attribute__((packed)) MCFG_t;

void mcfg_parse(uintptr_t addr);
boolean_t vxIsMCFGExisted();

#endif // __HAL__PCI__PCIE_H__