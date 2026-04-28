#include "ioforge/ioforge.h"
#include "block/block.h"
#include "hal/apic/ioapic.h"
#include "hal/cpu/core.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/paging.h"
#include "hal/timer/timer.h"
#include "init/init.h"
#include "ioforge/ioforge_pci.h"
#include "libk/debug/debug.h"
#include "libk/io.h"
#include "libk/symbols.h"
#include "libk/type.h"
#include "memory/kalloc.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "type.h"
#include <hal/pci/pci.h>
#include <libk/serial.h>
#include <libk/str.h>

struct ioforge_service* ioforge_services;
static uint64_t incremented_id = 1;
extern symbols ksymbols;

INIT(ioforge) {
	// enumerate pci
	pci_scan();
	KDEBUG(DEBUG_LEVEL_INFO, "PCI Scan done\n");
}

KERNEL_API
void ioforge_register_service(struct ioforge_service* service) {
	service->address = ((uint64_t)service->type << 56) |
	                   ((uint64_t)0xFF << 48) | incremented_id++;
	if (ioforge_services == 0) {
		ioforge_services = service;
		return;
	}

	serial2_printf("ioforge register ok 0x%x with addr %x\n", service,
	               service->address);
	struct ioforge_service* tmp = ioforge_services;
	while (tmp->next != 0) {
		tmp = tmp->next;
	}
	tmp->next = service;
}

KERNEL_API
struct ioforge_pci_service* ioforge_get_pci_device(uint16_t vendor_id,
                                                   uint16_t device_id) {
	// LOG_INFO("IOFORGE", "looking for pci device 0x%x", vendor_id);
	struct ioforge_service* tmp = ioforge_services;
	while (tmp != 0) {
		if (tmp->type == IOFORGE_PCI) {
			struct ioforge_pci_service* tmp_pci =
			    (struct ioforge_pci_service*)tmp;
			if (tmp_pci->vendor_id == vendor_id &&
			    tmp_pci->device_id == device_id) {
				LOG_INFO("IOFORGE", "found pci device 0x%x %x",
				         tmp_pci, tmp_pci->bar[0].address);
				return tmp_pci;
			}
		}
		tmp = tmp->next;
	}
	return 0;
}

KERNEL_API
void* ioforge_alloc(size_t size) { return kalloc(size); }

KERNEL_API
void ioforge_memset(void* ptr, uint8_t value, size_t num) {
	memset(ptr, value, num);
}

KERNEL_API
void ioforge_memcpy(void* dst, void* src, size_t num) {
	memcopy(dst, src, num);
}

KERNEL_API
void* ioforge_dma_alloc(size_t size, uintptr_t* paddr) {
	size_t aligned_size = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;
	// LOG_DEBUG("IOFORGE DMA", "alloc size %d", aligned_size);
	uintptr_t paddr_ = (uintptr_t)vxPhysBaseAlloc(aligned_size);
	uintptr_t vaddr = vma_lookup_free_vaddr(VMA_REGION_A, aligned_size);
	vxMultipleMmap(paging_get_highest_page_map(), vaddr, paddr_,
	               aligned_size, 0b111);
	paging_reload(paging_get_highest_page_map());
	vma_register(paddr_, vaddr, aligned_size * BLOCK_SIZE);
	if (paddr != 0) {
		*paddr = paddr_;
	}
	return (void*)vaddr;
}

KERNEL_API
void ioforge_dma_free(void* paddr, void* vaddr, size_t size) {
	vxPhysBaseFree(paddr, size);
	paging_unmap_fill(paging_get_highest_page_map(), (uintptr_t)vaddr,
	                  size);
	paging_reload(paging_get_highest_page_map());
	vma_unregister((uintptr_t)vaddr);
}

KERNEL_API
uintptr_t IOforgeMMapPhys(uintptr_t paddr, size_t size) {
	auto paddr_base = ALIGN_DOWN(paddr, PAGE_SIZE);
	auto offset_paddr = paddr - paddr_base;
	auto vaddr = vma_lookup_free_vaddr(VMA_REGION_A, size);
	vxMultipleMmap(paging_get_highest_page_map(), vaddr, paddr_base, size,
	               0b111);
	paging_reload(paging_get_highest_page_map());
	vma_register(paddr, vaddr, size / 4096);
	return vaddr + offset_paddr;
}

KERNEL_API
void ioforge_sleep(uint32_t time) { usleep(time); }

KERNEL_API
void ioforge_mmio_outl(uint32_t port, uint32_t value) {
	mmio_outl(port, value);
}

KERNEL_API
uint32_t ioforge_mmio_inl(uint32_t port) { return mmio_inl(port); }

KERNEL_API
void ioforge_irq_register(uint8_t n, void* handler) {
	auto core_id = coreGetCpuID();
	LOG2_INFO("IOFORGE", "registering irq %d on core %d", n, core_id);
	irq_register(core_id, n, handler, true, 0x28, 0, INTERRUPT_ATTR_KERNEL);
}

KERNEL_API
void ioforge_map_isr(uint8_t irq, uint8_t vector) {
	auto core_id = coreGetCpuID();
	LOG2_INFO("IOFORGE", "mapping isr %d on core %d", irq, core_id);
	vxIOAPICMapISR(irq, vector, core_id);
}

KERNEL_API
void IOforgeStrCopy(char* dst, char* src) { strcpy(dst, src); }

KERNEL_API
void registerBlockDevice(const char* name, block_device_operations_t* ops,
                         void* identifier) {
	// block_register_device(name, ops, identifier);
}
