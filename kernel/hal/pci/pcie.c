#include "hal/acpi/acpi.h"
#include "hal/cpu/paging.h"
#include "hal/pci/pci.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "memory/vm_manager.h"
#include <hal/pci/pcie.h>

static uint32_t ecam_read32(uintptr_t base, uint8_t bus, uint8_t dev,
			    uint8_t func, uint16_t offset) {
	uintptr_t addr = base + ((uintptr_t) bus << 20)
			 + ((uintptr_t) dev << 15) + ((uintptr_t) func << 12)
			 + offset;
	return *(volatile uint32_t*) addr;
}

static void ecam_write32(uintptr_t base, uint8_t bus, uint8_t dev, uint8_t func,
			 uint16_t offset, uint32_t val) {
	uintptr_t addr = base + ((uintptr_t) bus << 20)
			 + ((uintptr_t) dev << 15) + ((uintptr_t) func << 12)
			 + offset;
	*(volatile uint32_t*) addr = val;
}

static pci_access_ops_t ecam_ops = {
	.read32 = ecam_read32,
	.write32 = ecam_write32,
};

void register_segment(uint16_t seg_id, uint8_t start, uint8_t end,
		      uintptr_t vbase, pci_access_ops_t* ops,
		      PCI_SEGMENT_TYPE type);

boolean_t has_ecam = false;

void mcfg_parse(uintptr_t addr) {
	LOG2_INFO("PCIE", "found mcfg on 0x%x", addr);

	MCFG_t* mcfg = (MCFG_t*) addr;
	struct SDT* sdt = (struct SDT*) &mcfg->sdt;

	LOG_INFO("PCIE", "MCFG Length    : %d", sdt->Length);
	LOG_INFO("PCIE", "MCFG Signature : %s", sdt->Signature);

	size_t header_size = sizeof(struct SDT) + sizeof(uint64_t);
	size_t device_count = (sdt->Length - header_size)
			      / sizeof(MCFG_configuration_space_t);
	LOG_INFO("PCIE", "device count : %d", device_count);

	has_ecam = device_count > 0;

	for (size_t i = 0; i < device_count; i++) {
		MCFG_configuration_space_t* cs = &mcfg->conf[i];

		uint8_t start = cs->start_pci_bus;
		uint8_t end = cs->end_pci_bus;
		LOG_INFO("PCIE CONF", "start : %d  end : %d", start, end);

		size_t mapping_size = (size_t) (end - start + 1) * 1024 * 1024;
		size_t mapping_pages =
			(mapping_size + PAGE_SIZE - 1) / PAGE_SIZE;

		uintptr_t vaddr =
			vma_lookup_free_vaddr(VMA_REGION_C, mapping_pages);
		if (!vaddr) {
			LOG_INFO("pcie", "vaddr is 0");
		}
		LOG_INFO("PCIE CONF", "found vaddr : 0x%lx (%d KB)", vaddr,
			 mapping_pages * 4);

		vxMultipleMmap(paging_get_highest_page_map(), vaddr,
			       cs->base_addr, mapping_pages, 0b10011);
		paging_reload(paging_get_highest_page_map());

		LOG_INFO("PCIE CONF", "base addr : 0x%lx -> 0x%lx",
			 cs->base_addr, vaddr);

		uintptr_t adjusted_base = vaddr - ((uintptr_t) start << 20);

		register_segment(cs->pci_segment_group, cs->start_pci_bus,
				 cs->end_pci_bus, adjusted_base, &ecam_ops,
				 PCI_SEGMENT_PCIE);

		//  TODO:integrasikan dengan pci yang sudah ada
		// for (uint16_t bus = start; bus <= end; bus++) {
		// 	for (uint8_t dev = 0; dev < 32; dev++) {

		// 		/* Check function 0 first; bail early on absent
		// 		 * device */
		// 		uint32_t id0 =
		// 		    pcie_read32(vaddr, bus, dev, 0, 0x00);
		// 		uint16_t vendor = id0 & 0xFFFF;
		// 		if (vendor == 0xFFFF)
		// 			continue;

		// 		uint8_t header =
		// 		    (pcie_read32(vaddr, bus, dev, 0, 0x0C) >>
		// 		     16) &
		// 		    0xFF;
		// 		uint8_t func_count = (header & 0x80) ? 8 : 1;

		// 		for (uint8_t func = 0; func < func_count;
		// 		     func++) {

		// 			/* Reuse id0 for func 0; read fresh for
		// 			 * the rest */
		// 			uint32_t id =
		// 			    (func == 0)
		// 			        ? id0
		// 			        : pcie_read32(vaddr, bus, dev,
		// 			                     func, 0x00);
		// 			if ((id & 0xFFFF) == 0xFFFF)
		// 				continue;

		// 			uint32_t reg08 = pcie_read32(
		// 			    vaddr, bus, dev, func, 0x08);
		// 			uint8_t class_c = (reg08 >> 24) & 0xFF;
		// 			uint8_t subcls = (reg08 >> 16) & 0xFF;

		// 			auto vendor_id =
		// 			    (uint16_t)(id & 0xFFFF);
		// 			auto device_id =
		// 			    (uint16_t)((id >> 16) & 0xFFFF);

		// 			LOG_INFO("PCIE Device",
		// 			         "[%d:%d:%d] class=0x%x "
		// 			         "sub=0x02%x",
		// 			         bus, dev, func, class_c,
		// 			         (uint8_t)subcls);
		// 			LOG_INFO(
		// 			    "PCIE Device",
		// 			    "vendor id : 0x%x device id : 0x%x",
		// 			    vendor_id, device_id);
		// 		}
		// 	}
		// }
	}
}