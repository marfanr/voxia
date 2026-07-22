#include "./pci.h"
#include "hal/cpu/paging.h"
#include <type.h>
#include "memory/kalloc.h"
#include "memory/vm_manager.h"
#include <ioforge/ioforge.h>
#include <ioforge/ioforge_pci.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <str.h>
#include <memory/memory_utils.h>

static pci_segment_t segments[PCI_MAX_SEGMENTS];
static size_t segment_count = 0;
extern boolean_t has_ecam;

static pci_segment_t* find_segment(uint8_t bus) {
	for (size_t i = 0; i < segment_count; i++) {
		pci_segment_t* s = &segments[i];
		if (s->valid && bus >= s->bus_start && bus <= s->bus_end)
			return s;
	}
	return NULL; /* fallback ke segment 0 */
}

void register_segment(uint16_t seg_id, uint8_t start, uint8_t end,
		      uintptr_t vbase, pci_access_ops_t* ops,
		      PCI_SEGMENT_TYPE type) {
	if (segment_count >= PCI_MAX_SEGMENTS)
		return;
	pci_segment_t* s = &segments[segment_count++];
	s->segment_id = seg_id;
	s->bus_start = start;
	s->bus_end = end;
	s->vbase = vbase;
	s->ops = ops;
	s->valid = true;
	s->type = type;
}

//  legacy_ops
static pci_access_ops_t legacy_ops = {
	.read32 = legacy_read32,
	.write32 = legacy_write32,
};

// unified

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off) {
	pci_segment_t* s = find_segment(bus);
	if (!s)
		s = &segments[0]; /* fallback segment pertama */
	return s->ops->read32(s->vbase, bus, dev, func, off);
}

uint64_t pci_read64(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off) {
	uint32_t low = pci_read32(bus, dev, func, off & ~3);
	uint32_t high = pci_read32(bus, dev, func, (off & ~3) + 4);
	return ((uint64_t) high << 32) | low;
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off) {
	uint32_t dword = pci_read32(bus, dev, func, off & ~3);
	return (dword >> ((off & 2) * 8)) & 0xFFFF;
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off) {
	uint32_t dword = pci_read32(bus, dev, func, off & ~3);
	return (dword >> ((off & 3) * 8)) & 0xFF;
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off,
		 uint32_t val) {
	pci_segment_t* s = find_segment(bus);
	if (!s)
		s = &segments[0];
	s->ops->write32(s->vbase, bus, dev, func, off, val);
}

void pci_write64(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off,
		 uint64_t val) {
	uint32_t low = val & 0xFFFFFFFF;
	uint32_t high = val >> 32;
	pci_write32(bus, dev, func, off & ~3, low);
	pci_write32(bus, dev, func, (off & ~3) + 4, high);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off,
		 uint16_t val) {
	uint32_t dword = pci_read32(bus, dev, func, off & ~3);
	uint32_t shift = (off & 2) * 8;
	dword = (dword & (uint32_t) ~(0xFFFF << shift))
		| ((uint32_t) val << shift);
	pci_write32(bus, dev, func, off & ~3, dword);
}

void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off,
		uint8_t val) {
	uint32_t dword = pci_read32(bus, dev, func, off & ~3);
	uint32_t shift = (off & 3) * 8;
	dword = (dword & (uint32_t) ~(0xFF << shift))
		| ((uint32_t) val << shift);
	pci_write32(bus, dev, func, off & ~3, dword);
}
//

// Sekarang: hanya deteksi MSI ada/tidak
// Yang berguna: actually enable MSI supaya tidak pakai legacy IRQ

static void vxPCIGatheringBusInfo(uint8_t bus, uint8_t device, uint8_t func) {
	struct ioforge_pci_device* pci = (struct ioforge_pci_device*) kalloc(
		sizeof(struct ioforge_pci_device));
	memset(pci, 0, sizeof(*pci));

	pci->pci_bus = (size_t) bus;
	pci->pci_dev = (size_t) device;
	pci->pci_func = (size_t) func;

	pci->device = device;

	pci->base.type = IOFORGE_PCI;
	pci->vendor_id = pci_read16(bus, device, func, 0);
	pci->device_id = pci_read16(bus, device, func, 2);
	LOG_INFO("PCI", "device id : 0x%x", pci->device_id);
	LOG_INFO("PCI", "vendor id : 0x%x", pci->vendor_id);
	pci->command = pci_read16(bus, device, func, 4);

	pci->status = pci_read16(bus, device, func, 6);
	if ((pci->status & (1 << 4))) {
		uint8_t cap_ptr = pci_read8(bus, device, func, 0x34);
		pci->capability_ptr = cap_ptr;
	}
	pci->revision_id = pci_read16(bus, device, func, 8) & 0xFF;
	pci->prog_if = (pci_read16(bus, device, func, 8) >> 8) & 0xFF;
	pci->subclass = pci_read16(bus, device, func, 10) & 0xFF;
	pci->classes = (pci_read16(bus, device, func, 10) >> 8) & 0xFF;

	// uint8_t cache_line_size = pci_read16(bus, device, func, 12) & 0xFF;
	// uint8_t latency_timer = pci_read16(bus, device, func, 13) & 0xFF;
	// uint8_t header_type = pci_read16(bus, device, func, 14) & 0xFF;
	// uint8_t bist = pci_read16(bus, device, func, 15) & 0xFF;

	for (int i = 0; i < 6; i++) {
		uint32_t bar_low = pci_read32(bus, device, func, (uint16_t)(0x10 + i * 4));
		if (bar_low == 0) continue;

		// Disable memory/IO space during probing
		uint16_t cmd = pci_read16(bus, device, func, 0x04);
		pci_write16(bus, device, func, 0x04, cmd & ~0x7);

		int bar_idx = i;
		uint64_t addr = 0;
		uint64_t size = 0;
		boolean_t is_io = (bar_low & 1);
		boolean_t is_64bit = false;

		if (is_io) {
			addr = bar_low & ~3U;
			pci_write32(bus, device, func, (uint16_t)(0x10 + i * 4), 0xFFFFFFFF);
			uint32_t val = pci_read32(bus, device, func, (uint16_t)(0x10 + i * 4));
			size = (uint32_t)(~(val & ~3U) + 1);
			pci_write32(bus, device, func, (uint16_t)(0x10 + i * 4), bar_low);

			pci->bar[bar_idx].iospace = 1;
			pci->bar[bar_idx].address = addr;
			LOG_INFO("PCI", "[%d] BAR IO 0x%lx size: %ld", bar_idx, addr, size);
		} else {
			addr = bar_low & ~0xFUL;
			is_64bit = ((bar_low & 0x6) == 0x4);
			
			uint32_t bar_high = 0;
			if (is_64bit && i < 5) {
				bar_high = pci_read32(bus, device, func, (uint16_t)(0x10 + (i + 1) * 4));
				addr |= ((uint64_t)bar_high << 32);
			}

			// Probe size
			pci_write32(bus, device, func, (uint16_t)(0x10 + i * 4), 0xFFFFFFFF);
			if (is_64bit && i < 5) {
				pci_write32(bus, device, func, (uint16_t)(0x10 + (i + 1) * 4), 0xFFFFFFFF);
				uint64_t val64 = (uint64_t)pci_read32(bus, device, func, (uint16_t)(0x10 + i * 4)) |
								 ((uint64_t)pci_read32(bus, device, func, (uint16_t)(0x10 + (i + 1) * 4)) << 32);
				size = ~(val64 & ~0xFUL) + 1;
				pci_write32(bus, device, func, (uint16_t)(0x10 + (i + 1) * 4), bar_high);
			} else {
				uint32_t val = pci_read32(bus, device, func, (uint16_t)(0x10 + i * 4));
				size = (uint32_t)(~(val & ~0xFUL) + 1);
			}
			pci_write32(bus, device, func, (uint16_t)(0x10 + i * 4), bar_low);

			// MMIO Mapping
			if (size > 0) {
				uint64_t map_size = size;

				uintptr_t paddr_aligned = ALIGN_DOWN(addr, PAGE_SIZE_4KB);
				uint32_t offset = (uint32_t)(addr - paddr_aligned);
				uint64_t total_pages = (offset + map_size + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;

				uintptr_t vaddr = vma_lookup_free_vaddr(get_kernel_vmm_page(), VMA_REGION_B, (size_t)total_pages);
				if (!vaddr) {
					LOG_ERROR("PCI", "Failed to allocate VMA for BAR %d", bar_idx);
					continue;
				}
				uint64_t map_flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH | PAGE_NO_EXECUTE;
				
				paging_multiple_mmap(paging_get_highest_page_map(), vaddr, paddr_aligned, total_pages, map_flags);
				paging_reload(paging_get_highest_page_map());
				vma_register(get_kernel_vmm_page(), paddr_aligned, vaddr, total_pages * PAGE_SIZE_4KB, map_flags);

				pci->bar[bar_idx].iospace = 0;
				pci->bar[bar_idx].address = vaddr + offset;
				
				LOG_INFO("PCI", "[%d] BAR %s 0x%lx -> 0x%lx size: %ld KB (mapped %ld KB)", 
						 bar_idx, is_64bit ? "MEM64" : "MEM32", addr, (uintptr_t)pci->bar[bar_idx].address, (uint32_t)size / 1024, (uint32_t)total_pages * 4);
			}

			if (is_64bit) i++;
		}
		pci_write16(bus, device, func, 0x04, cmd); // Restore command
	}

	// Enable Bus Master and Memory Space for all devices by default
	{
		auto cmd = pci->command;
		cmd |= (1 << 2); // Bus Master
		cmd |= (1 << 1); // Memory Space
		pci_write16(bus, device, func, 4, cmd);

		// check
		cmd = pci_read16(bus, device, func, 4);
		auto bus_master = (cmd & (1 << 2)) >> 2;
		auto memory_space = (cmd & (1 << 1)) >> 1;
		LOG_INFO("PCI", "bus master : %d", bus_master);
		LOG_INFO("PCI", "memory space : %d", memory_space);
	}

	// uint32_t cardbus_cis_pointer =
	// 	pci_read16(bus, device, func, 41) & 0xFFFF;
	// cardbus_cis_pointer |= pci_read16(bus, device, func, 43) << 16;
	// uint16_t subsystem_vendor_id = pci_read16(bus, device, func, 44);
	// uint16_t subsystem_id = pci_read16(bus, device, func, 46);
	// uint32_t expansion_rom_base_address =
	// 	pci_read16(bus, device, func, 48) & 0xFFFF;
	// expansion_rom_base_address |= pci_read16(bus, device, func, 50) << 16;
	// pci->capability_ptr    = pci_read16(bus, device, func, 52);

	uint8_t interrupt_line = pci_read16(bus, device, func, 60) & 0xFF;
	uint8_t interrupt_pin = (pci_read16(bus, device, func, 60) >> 8) & 0xFF;
	LOG_INFO("PCI", "IRQ %d pin : %d", (int)interrupt_line, (int)interrupt_pin);
	pci->interrupt_line = interrupt_line;
	pci->interrupt_pin = interrupt_pin;

	// uint8_t min_grant = pci_read16(bus, device, func, 62) & 0xFF;
	// uint8_t max_latency = pci_read16(bus, device, func, 63) & 0xFF;

	LOG_INFO("PCI", "class : 0x%x subclass : 0x%x", (uint32_t)pci->classes,
		 (uint32_t)pci->subclass);

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
		LOG_INFO("PCI", "virtio device found w device_id : 0x%x",
			 pci->device_id);
		switch (pci->device_id) {
		case 0x1050:
			LOG_INFO("PCI", "found virtio VGA device");
			pci->base.flags |= (uint32_t) IOFORGE_F_VIRTIO;
			// pci->service.init = (void (*)(struct IoForgeService
			// *))virtio_gpu_init;
			break;
		}
		break;
	}
	}
	serial_printf("\n");

	ioforge_attach(ioforge_get_pci_root(), &pci->base);
}

static void pci_check_bus(uint8_t bus);

static void pci_check_func(uint8_t bus, uint8_t dev, uint8_t func) {
	uint8_t class = pci_read8(bus, dev, func, 0x0B);
	uint8_t subclass = pci_read8(bus, dev, func, 0x0A);

	if (class == 0x06 && subclass == 0x04) {
		serial_trace("PCI-to-PCI bridge\n");
		uint8_t sec_bus = pci_read8(bus, dev, func, 0x19);
		pci_check_bus(sec_bus);
	} else {
		vxPCIGatheringBusInfo(bus, dev, func);
	}
}

static void pci_check_bus(uint8_t bus) {
	for (uint8_t dev = 0; dev < 32; dev++) {
		uint32_t id0 = pci_read32(bus, dev, 0, 0x00);
		uint16_t vendor = id0 & 0xFFFF;
		if (vendor == 0xFFFF || vendor == 0x0000)
			continue;

		uint8_t header = (pci_read32(bus, dev, 0, 0x0C) >> 16) & 0xFF;
		uint8_t func_count = (header & 0x80) ? 8 : 1;

		for (uint8_t func = 0; func < func_count; func++) {
			uint32_t id =
				(func == 0) ? id0
					    : pci_read32(bus, dev, func, 0x00);
			if ((id & 0xFFFF) == 0xFFFF || (id & 0xFFFF) == 0x0000)
				continue;

			LOG_INFO("PCI", "device found %d:%d:%d", bus, dev,
				 func);
			pci_check_func(bus, dev, func);
		}
	}
}

extern boolean_t has_ecam;

static void pci_scan_bus(pci_segment_t* s) {
	uint8_t host_header = pci_read8(s->bus_start, 0, 0, 0x0E);

	if (host_header & 0x80) {
		// Multi-function host bridge: scan each function on device 0
		for (uint8_t func = 0; func < 8; func++) {
			if (pci_read16(s->bus_start, 0, func, 0) == 0xFFFF)
				continue;
			pci_check_func(s->bus_start, 0, func);
		}
	} else {
		// Single-function host bridge: start normal traversal
		pci_check_bus(s->bus_start);
	}
}

void pci_scan() {
	LOG_INFO("PCI", "PCI scan sequence starting...");
	if (!has_ecam) {
		LOG_INFO("PCI", "No MCFG found, falling back to legacy PCI (I/O ports)");
		register_segment(0, 0, 255, 0, &legacy_ops, PCI_SEGMENT_LEGACY);
	} else {
		LOG_INFO("PCI", "MCFG found, using PCIe ECAM for configuration space");
	}

	for (size_t i = 0; i < segment_count; i++) {
		pci_segment_t* s = &segments[i];
		if (!s->valid)
			continue;

		LOG_INFO("PCI", "Scanning segment %d (bus %d-%d)",
			 s->segment_id, s->bus_start, s->bus_end);

		pci_scan_bus(s);
	}
}
