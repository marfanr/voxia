#include "./pci.h"
#include "hal/cpu/paging.h"
#include "memory/kalloc.h"
#include "memory/vm_manager.h"
#include <hal/graphic/virtio.h>
#include <ioforge/ioforge.h>
#include <ioforge/ioforge_pci.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/memory_utils.h>

#include <hal/usb/ehci.h>

static void pci_check_bus(size_t bus);
static void vxPCIGatheringBusInfo(size_t bus, size_t device, size_t func);

static void pci_check_func(size_t bus, size_t device, size_t func) {
	uint8_t class = pci_readw(bus, device, func, 0x0A) >> 8;
	uint8_t subclass = pci_readw(bus, device, func, 0x0A) & 0xFF;

	if (class == 0x06 && subclass == 0x04) {
		serial_trace("PCI to PCI bridge found\n");
		uint8_t secondary_bus = pci_readw(bus, device, func, 0x19);
		pci_check_bus(secondary_bus);
	} else {
		vxPCIGatheringBusInfo(bus, device, func);
	}
}

static void vxPCIGatheringBusInfo(size_t bus, size_t device, size_t func) {
	struct ioforge_pci_service* pci = (struct ioforge_pci_service*)kalloc(
	    sizeof(struct ioforge_pci_service));
	memset(pci, 0, sizeof(struct ioforge_pci_service));
	// serial_trace("\npci %d at 0x%x\n\n", bus, pci);

	pci->pci_bus = bus;
	pci->pci_dev = device;
	pci->pci_func = func;

	pci->device = device;

	pci->service.type = IOFORGE_PCI;
	pci->vendor_id = pci_readw(bus, device, func, 0);
	pci->device_id = pci_readw(bus, device, func, 2);
	LOG_INFO("PCI", "device id : 0x%x", pci->device_id);
	LOG_INFO("PCI", "vendor id : 0x%x", pci->vendor_id);
	pci->command = pci_readw(bus, device, func, 4);
	// turn on bus mastering

	// sementara untuk e1000 saja
	// if (pci->vendor_id == 0x8086 && pci->device_id == 0x100C) {
	if (pci->vendor_id == 0x8086 && pci->device_id == 0x10D3) {
		auto cmd = pci->command;
		cmd |= (1 << 2); // Bus Master
		// cmd |= (1 << 1);   // Memory Space
		// cmd |= ~(1 << 10); // Memory Space
		pci_writew(bus, device, func, 4, cmd);

		// check
		cmd = pci_readw(bus, device, func, 4);
		auto bus_master = (cmd & (1 << 2)) >> 2;
		auto memory_space = (cmd & (1 << 1)) >> 1;
		LOG_INFO("PCI", "bus master : %d", bus_master);
		LOG_INFO("PCI", "memory space : %d", memory_space);
	}

	pci->status = pci_readw(bus, device, func, 6);
	if ((pci->status & (1 << 4))) {
		uint8_t cap_ptr = pci_readb(bus, device, func, 0x34);
		pci->capability_ptr = cap_ptr;
		// LOG_INFO("PCI", "device ada capability list");

		while (cap_ptr != 0 && cap_ptr >= 0x40 && cap_ptr <= 0xFF) {
			uint8_t cap_id = pci_readb(bus, device, func,
			                           cap_ptr); // ID capability
			uint8_t next_ptr = pci_readb(
			    bus, device, func,
			    cap_ptr + 1); // pointer ke capability selanjutnya

			if (cap_id == 05) {
				LOG_INFO("PCI", "MSI Available");
			}

			cap_ptr = next_ptr;
		}
	}
	pci->revision_id = pci_readw(bus, device, func, 8) & 0xFF;
	pci->prog_if = (pci_readw(bus, device, func, 8) >> 8) & 0xFF;
	pci->subclass = pci_readw(bus, device, func, 10) & 0xFF;
	pci->classes = (pci_readw(bus, device, func, 10) >> 8) & 0xFF;

	uint8_t cache_line_size = pci_readw(bus, device, func, 12) & 0xFF;
	uint8_t latency_timer = pci_readw(bus, device, func, 13) & 0xFF;
	uint8_t header_type = pci_readw(bus, device, func, 14) & 0xFF;

	uint8_t bist = pci_readw(bus, device, func, 15) & 0xFF;
	for (int i = 0; i < 6; i++) {

		uint32_t bar = pci_readl(bus, device, func, 0x10 + i * 4);
		int bar_idx = i; // simpan index asli sebelum i++

		if (bar & 1) {
			// I/O space
			pci->bar[i].iospace = 1;
			pci->bar[i].address = bar & ~3;
			LOG_INFO("PCI", "[%d] BAR IO 0x%x (0x%x)", i, bar,
			         pci->bar[i].address);
			continue;
		}

		// Memory space
		pci->bar[i].iospace = 0;
		pci->bar[i].address = bar & ~0xF;

		// Check 64-bit BAR
		uint64_t addr = pci->bar[i].address;
		if ((bar & 0x6) == 0x4) {
			uint32_t bar_high =
			    pci_readl(bus, device, func, 0x10 + (i + 1) * 4);
			addr |= ((uint64_t)bar_high << 32);
			i++; // skip next BAR
		}

		uint32_t original_bar = bar;
		pci_writel(bus, device, func, 0x10 + bar_idx * 4, 0xFFFFFFFF);
		uint32_t value =
		    pci_readl(bus, device, func, 0x10 + bar_idx * 4);
		uint32_t size = ~(value & ~0xF) + 1;
		pci_writel(bus, device, func, 0x10 + bar_idx * 4, original_bar);

		uint32_t size_4kb = (size + PAGE_SIZE - 1) / PAGE_SIZE;

		uintptr_t vaddr = bar;
		if (original_bar) {
			vaddr = vma_lookup_free_vaddr(VMA_REGION_C, size_4kb);
			// LOG_INFO("PCI", "new vadr 0x%x", vaddr);

			vxMultipleMmap(paging_get_highest_page_map(), vaddr,
			               pci->bar[i].address, size_4kb, 0b10011);
			paging_reload(paging_get_highest_page_map());
			vma_register(addr, vaddr, size_4kb * PAGE_SIZE);
		}
		uint32_t offset = addr - ALIGN_DOWN(addr, PAGE_SIZE);
		LOG_INFO("PCI", "[%d] BAR 0x%x [0x%x] (0x%x) size: %d KB", i,
		         original_bar, vaddr, offset, size_4kb * 4);

		pci->bar[bar_idx].address = vaddr;
	}

	uint32_t cardbus_cis_pointer =
	    pci_readw(bus, device, func, 41) & 0xFFFF;
	cardbus_cis_pointer |= pci_readw(bus, device, func, 43) << 16;
	uint16_t subsystem_vendor_id = pci_readw(bus, device, func, 44);
	uint16_t subsystem_id = pci_readw(bus, device, func, 46);
	uint32_t expansion_rom_base_address =
	    pci_readw(bus, device, func, 48) & 0xFFFF;
	expansion_rom_base_address |= pci_readw(bus, device, func, 50) << 16;
	// pci->capability_ptr    = pci_readw(bus, device, func, 52);
	uint8_t interrupt_line = pci_readw(bus, device, func, 60) & 0xFF;
	uint8_t interrupt_pin = pci_readw(bus, device, func, 60) >> 8;
	uint8_t min_grant = pci_readw(bus, device, func, 62) & 0xFF;
	uint8_t max_latency = pci_readw(bus, device, func, 63) & 0xFF;

	LOG_INFO("PCI", "class : 0x%x subclass : 0x%x", pci->classes,
	         pci->subclass);

	// initialize standard supported device
	switch (pci->classes) {
	case 0x00:
		switch (pci->subclass) {
		case 0x00:
			LOG_INFO("PCI", "Unclassified device");
			break;
		case 0x01:
			LOG_INFO("PCI", "Mass storage controller");
			break;
		case 0x02:
			LOG_INFO("PCI", "Network controller");
			break;
		case 0x03:
			LOG_INFO("PCI", "Display controller");
			break;
		case 0x04:
			LOG_INFO("PCI", "Multimedia controller");
			break;
		case 0x05:
			LOG_INFO("PCI", "Memory controller");
			break;
		case 0x06:
			LOG_INFO("PCI", "Bridge device");
			break;
		case 0x07:
			LOG_INFO("PCI", "Simple communication controller");
			break;
		case 0x08:
			LOG_INFO("PCI", "Base system peripheral");
			break;
		case 0x09:
			LOG_INFO("PCI", "Input device controller");
			break;
		case 0x0A:
			LOG_INFO("PCI", "Docking station");
			break;
		case 0x0B:
			LOG_INFO("PCI", "Processor");
			break;
		case 0x0C:
			LOG_INFO("PCI", "Serial bus controller");
			break;
		case 0x0D:
			LOG_INFO("PCI", "Wireless controller");
			break;
		case 0x0E:
			LOG_INFO("PCI", "Intelligent controller");
			break;
		case 0x0F:
			LOG_INFO("PCI", "Satellite communication controller");
			break;
		case 0x10:
			LOG_INFO("PCI", "Encryption controller");
			break;
		case 0x11:
			LOG_INFO("PCI", "Signal processing controller");
			break;
		case 0x12:
			LOG_INFO("PCI", "Processing accelerators");
			break;
		case 0x13:
			LOG_INFO("PCI", "Non-Essential Instrumentation");
			break;
		case 0x40:
			LOG_INFO("PCI", "Co-processor");
			break;
		case 0xFF:
			LOG_INFO("PCI",
			         "Device does not fit in any defined class");
			break;
		default:
			LOG_INFO("PCI", "Unknown class");
			break;
		}
		break;
	case 0x01:
		switch (pci->subclass) {
		case 0x00:
			LOG_INFO("PCI", "SCSI storage controller");
			break;
		case 0x01:
			LOG_INFO("PCI", "IDE interface");
			break;
		}
		break;
	case 0x02:
		switch (pci->subclass) {
		case 0x00:
			LOG_INFO("PCI", "Ethernet controller");
			break;
		}
		break;
	case 0xC:
		switch (pci->subclass) {
		case 0x3:
			switch (pci->prog_if) {
			case 0x20:
				LOG_INFO("PCI", "EHCI device");
				/* pci->service.init = ehci_init; */
				break;
			}
			break;
		}
		break;
	}

	switch (pci->vendor_id) {
	case 0x1AF4: {
		// serial_trace("virtio device found w device_id : 0x%x\n",
		// pci->device_id);
		switch (pci->device_id) {
		case 0x1050:
			LOG_INFO("PCI", "found virtio VGA device");
			// pci->service.init = (void (*)(struct IoForgeService
			// *))virtio_gpu_init;
			break;
		}
		break;
	}
	}
	serial_printf("\n");
	ioforge_register_service((struct ioforge_service*)pci);
}

static void pci_check_bus(size_t bus) {
	for (size_t device = 0; device < 32; device++) {
		uint16_t vendor_id = pci_readw(bus, device, 0, 0);
		if (vendor_id == 0xFFFF)
			continue;

		uint8_t header_type = pci_readw(bus, device, 0, 0x0E) & 0xFF;

		if (header_type & 0x80) {
			for (size_t func = 0; func < 8; func++) {
				uint16_t vendor_id =
				    pci_readw(bus, device, func, 0);
				if (vendor_id == 0xFFFF)
					continue;

				LOG_INFO("PCI", "PCI device found at %d:%d:%d",
				         bus, device, func);
				//  check if it is a PCI to PCI bridge
				pci_check_func(bus, device, func);
			}
		} else {
			// LOG_INFO("PCI", "PCI device found at %d:%d", bus,
			// device);
			vxPCIGatheringBusInfo(bus, device, 0);
		}
	}
}

void pci_scan() {
	uint8_t header_type = pci_readw(0, 0, 0, 0x0E) & 0xFF;

	if (header_type & 0x80) {
		for (size_t func = 0; func < 8; func++) {
			uint16_t vendor_id = pci_readw(0, 0, func, 0);
			if (vendor_id == 0xFFFF)
				break;
			pci_check_bus(func);
		}
	} else {
		pci_check_bus(0);
	}
}
