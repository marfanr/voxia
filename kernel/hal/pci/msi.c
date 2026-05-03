#include "ioforge/ioforge_pci.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "pci.h"

void KERNEL_API pci_enable_msi(struct ioforge_pci_service* pci, uint8_t vector,
			       uint8_t cpu, uint16_t cap) {
	if (!cap)
		return;

	// disable msi
	uint16_t ctrl = pci_read16(pci->pci_bus, pci->pci_dev, pci->pci_func,
				   cap + 0x2);
	LOG2_INFO("MSI", "ctrl=0x%x", ctrl);
	pci_write16(pci->pci_bus, pci->pci_dev, pci->pci_func, cap + 0x2,
		    ctrl & ~1);

	// mirip apic
	uint32_t msg_addr = 0xFEE00000 | (cpu << 12);
	uint16_t msg_data = (vector & 0xFF);

	if (ctrl & (1 << 7)) {
		// 64-bit
		pci_write32(pci->pci_bus, pci->pci_dev, pci->pci_func,
			    cap + 0x4, msg_addr);
		pci_write32(pci->pci_bus, pci->pci_dev, pci->pci_func,
			    cap + 0x8, 0); // upper addr
		pci_write16(pci->pci_bus, pci->pci_dev, pci->pci_func,
			    cap + 0xC, msg_data);
	} else {
		// 32-bit
		pci_write32(pci->pci_bus, pci->pci_dev, pci->pci_func,
			    cap + 0x4, msg_addr);
		pci_write16(pci->pci_bus, pci->pci_dev, pci->pci_func,
			    cap + 0x8, msg_data);
	}

	// Enable MSI bit
	__asm__ volatile("mfence" ::: "memory");
	pci_write16(pci->pci_bus, pci->pci_dev, pci->pci_func, cap + 0x2,
		    (ctrl & ~(0x70)) | 1);

	ctrl = pci_read16(pci->pci_bus, pci->pci_dev, pci->pci_func, cap + 0x2);
	LOG2_INFO("MSI", "ctrl=0x%x", ctrl);

	LOG2_INFO("PCI", "MSI enabled vector=%d cpu=%d", vector, cpu);
}

uintptr_t KERNEL_API pci_enable_msix(struct ioforge_pci_service* pci,
				     uint8_t vector, uint8_t cpu, uint8_t cap) {
	if (!cap)
		return 0;

	uint16_t ctrl = pci_read16(pci->pci_bus, pci->pci_dev, pci->pci_func,
				   cap + 0x2);
	LOG2_INFO("MSIX", "enabled, ctrl=0x%x", ctrl);
	pci_write16(pci->pci_bus, pci->pci_dev, pci->pci_func, cap + 0x2,
		    ctrl & ~(1 << 15));

	uint32_t table_info = pci_read32(pci->pci_bus, pci->pci_dev,
					 pci->pci_func, cap + 0x4);
	uint8_t bir = table_info & 0x7;
	uint32_t offset = table_info & ~0x7;

	auto table_size = (ctrl & 0x7FF) + 1; // ← +1: table_size adalah N+1
	LOG2_INFO("MSIX", "Table size : %d", table_size);

	auto selected_bar = pci->bar[bir].address + offset;
	LOG2_INFO("MSIX", "bir at 0x%x with offset 0x%x -> 0x%x", bir, offset,
		  selected_bar);

	if (!selected_bar)
		return 0;

	volatile uint32_t* msix_table = (volatile uint32_t*) selected_bar;
	for (int i = 0; i < table_size; i++) {
		msix_table[i * 4 + 0] = 0xFEE00000 | (cpu << 12); // Address Low
		msix_table[i * 4 + 1] = 0;		 // Address High
		msix_table[i * 4 + 2] = (vector & 0xFF); // Data
		msix_table[i * 4 + 3] = 0; // Vector Control (0 = Unmasked)
	}

	__asm__ volatile("mfence" ::: "memory");

	// Bit 15 = enable, Bit 14 harus 0 (Function Mask OFF)
	pci_write16(pci->pci_bus, pci->pci_dev, pci->pci_func, cap + 0x2,
		    (ctrl | (1 << 15)) & ~(1 << 14));

	LOG2_INFO("MSIX", "enabled, ctrl=0x%x",
		  pci_read16(pci->pci_bus, pci->pci_dev, pci->pci_func,
			     cap + 0x2));

	return selected_bar;
}

uint16_t KERNEL_API pci_cap_find_msi(struct ioforge_pci_service* pci) {
	auto cap_ptr = pci->capability_ptr;
	auto bus = pci->pci_bus;
	auto device = pci->pci_dev;
	auto func = pci->pci_func;

	while (cap_ptr != 0 && cap_ptr >= 0x40 && cap_ptr <= 0xFF) {
		uint8_t cap_id = pci_read8(bus, device, func,
					   cap_ptr); // ID capability
		uint8_t next_ptr = pci_read8(
			bus, device, func,
			cap_ptr + 1); // pointer ke capability selanjutnya

		if (cap_id == 0x05) {
			return cap_ptr;
		}

		cap_ptr = next_ptr;
	}
	return 0;
}

uint16_t KERNEL_API pci_cap_find_msix(struct ioforge_pci_service* pci) {
	auto cap_ptr = pci->capability_ptr;
	auto bus = pci->pci_bus;
	auto device = pci->pci_dev;
	auto func = pci->pci_func;

	while (cap_ptr != 0 && cap_ptr >= 0x40 && cap_ptr <= 0xFF) {
		uint8_t cap_id = pci_read8(bus, device, func,
					   cap_ptr); // ID capability
		uint8_t next_ptr = pci_read8(
			bus, device, func,
			cap_ptr + 1); // pointer ke capability selanjutnya

		if (cap_id == 0x11) {
			return cap_ptr;
		}

		cap_ptr = next_ptr;
	}
	return 0;
}