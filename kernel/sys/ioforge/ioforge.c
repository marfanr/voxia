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
#include <ioforge/ioforge_usb.h>

static struct ioforge_device* root = 0;
struct ioforge_device* pci_root = 0;
struct ioforge_device* usb_controller_root = 0;
struct ioforge_device* usb_devices_root = 0;

// static uint64_t incremented_id = 1;

void KERNEL_API print_device_tree(struct ioforge_device* node, int indent) {
	if (!node)
		return;

	for (int i = 0; i < indent; i++)
		serial2_printf(" ");

	if (node->type == IOFORGE_PCI) {
		struct ioforge_pci_device* pd =
			(struct ioforge_pci_device*) node;
		serial2_printf("pci %d:%d\n", pd->vendor_id, pd->device_id);
	} else if (node->type == IOFORGE_USB_DEVICE) {
		struct ioforge_usb_service* d =
			(struct ioforge_usb_service*) node;
		serial2_printf("usb device: %s %s\n", d->base.name,
			       d->serial_number);
	} else {
		serial2_printf("device: %s\n", node->name);
	}

	print_device_tree(node->first_child, indent + 1);
	print_device_tree(node->next_sibling, indent);
}

INIT(ioforge) {
	/* setup tree */
	root = kalloc(sizeof(struct ioforge_device));
	memset(root, 0, sizeof(*root));
	strcpy((char*) root->name, "ROOT");
	root->type = IOFORGE_ROOT;

	struct ioforge_device* acpi_root =
		kalloc(sizeof(struct ioforge_device));
	memset(acpi_root, 0, sizeof(*acpi_root));
	strcpy((char*) acpi_root->name, "_SB");
	acpi_root->type = IOFORGE_ROOT;
	ioforge_attach(root, acpi_root);

	pci_root = kalloc(sizeof(struct ioforge_device));
	memset(pci_root, 0, sizeof(*pci_root));
	strcpy((char*) pci_root->name, "PCI");
	pci_root->type = IOFORGE_ROOT;
	ioforge_attach(acpi_root, pci_root);

	usb_controller_root = kalloc(sizeof(struct ioforge_device));
	memset(usb_controller_root, 0, sizeof(*usb_controller_root));
	strcpy((char*) usb_controller_root->name, "USB_CTRL");
	usb_controller_root->type = IOFORGE_ROOT;
	ioforge_attach(root, usb_controller_root);

	usb_devices_root = kalloc(sizeof(struct ioforge_device));
	memset(usb_devices_root, 0, sizeof(*usb_devices_root));
	strcpy((char*) usb_devices_root->name, "USB_DEV");
	usb_devices_root->type = IOFORGE_ROOT;
	ioforge_attach(root, usb_devices_root);

	// enumerate pci
	pci_scan();
	KDEBUG(DEBUG_LEVEL_INFO, "PCI Scan done\n");
}

void KERNEL_API ioforge_attach(struct ioforge_device* parent,
			       struct ioforge_device* child) {

	if (!parent || !child)
		return;

	child->parent = parent;
	child->next_sibling = parent->first_child;
	child->prev_sibling = 0;

	if (parent->first_child)
		parent->first_child->prev_sibling = child;

	parent->first_child = child;
	child->flags |= IOFORGE_F_ENABLE;
}

struct ioforge_device* KERNEL_API
ioforge_find_by_name(struct ioforge_device* root, const char* name) {
	if (!root)
		return NULL;

	if (strncmp(root->name, name, 64) == 0)
		return root;

	/* Cari ke children dulu */
	struct ioforge_device* child = root->first_child;
	// LOG_INFO("ioforge", "search on first child %s", child->name);
	while (child) {
		struct ioforge_device* found =
			ioforge_find_by_name(child, name);
		if (found)
			return found;
		child = child->next_sibling;
	}

	return NULL;
}

struct ioforge_device* KERNEL_API ioforge_get_pci_root() {
	return pci_root;
}

struct ioforge_device* KERNEL_API ioforge_get_usb_ctrl_root() {
	return usb_controller_root;
}

struct ioforge_device* KERNEL_API ioforge_get_usb_devices_root() {
	return usb_devices_root;
}

static bool ioforge_can_contain_pci(IoForgeType type) {
	switch (type) {
	case IOFORGE_ROOT:
	case IOFORGE_ACPI:
	case IOFORGE_PCI_BUS:
	case IOFORGE_PCI: /* PCI bridge bisa punya PCI child */
		return true;
	default:
		return false; /* USB, NIC, UART, dll → skip */
	}
}

struct ioforge_pci_device* KERNEL_API ioforge_find_pci_device(
	struct ioforge_device* node, uint16_t vendor_id, uint16_t device_id) {

	if (!node)
		return NULL;

	if (node->type == IOFORGE_PCI) {
		struct ioforge_pci_device* pci =
			(struct ioforge_pci_device*) node;
		if (pci->vendor_id == vendor_id && pci->device_id == device_id)
			return pci;
	}

	if (!ioforge_can_contain_pci(node->type))
		return NULL; /* ← pruning, tidak masuk ke sini */

	struct ioforge_device* child = node->first_child;
	while (child) {
		struct ioforge_pci_device* found =
			ioforge_find_pci_device(child, vendor_id, device_id);
		if (found)
			return found;
		child = child->next_sibling;
	}

	return NULL;
}

struct ioforge_device* KERNEL_API ioforge_get_root() {
	return root;
}

void* KERNEL_API ioforge_alloc(size_t size) {
	return kalloc(size);
}

void KERNEL_API ioforge_free(void* ptr, size_t size) {
	kfree(ptr, size);
}

void KERNEL_API ioforge_memset(void* ptr, uint8_t value, size_t num) {
	memset(ptr, value, num);
}

void KERNEL_API ioforge_memcpy(void* dst, void* src, size_t num) {
	memcopy(dst, src, num);
}

void* KERNEL_API ioforge_dma_alloc(size_t size, uintptr_t* paddr) {
	size_t aligned_size = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;
	// LOG_DEBUG("IOFORGE DMA", "alloc size %d", aligned_size);
	uintptr_t paddr_ = (uintptr_t) vxPhysBaseAlloc(aligned_size);
	uintptr_t vaddr = vma_lookup_free_vaddr(VMA_REGION_A, aligned_size);
	vxMultipleMmap(paging_get_highest_page_map(), vaddr, paddr_,
		       aligned_size, 0b111);
	paging_reload(paging_get_highest_page_map());
	vma_register(paddr_, vaddr, aligned_size * BLOCK_SIZE);
	if (paddr != 0) {
		*paddr = paddr_;
	}
	return (void*) vaddr;
}

KERNEL_API
void ioforge_dma_free(void* paddr, void* vaddr, size_t size) {
	size_t aligned_size = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;
	vxPhysBaseFree(paddr, aligned_size);
	paging_unmap_fill(paging_get_highest_page_map(), (uintptr_t) vaddr,
			  aligned_size);
	paging_reload(paging_get_highest_page_map());
	vma_unregister((uintptr_t) vaddr);
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
void ioforge_sleep(uint32_t time) {
	usleep(time);
}

KERNEL_API
void ioforge_mmio_outl(uint32_t port, uint32_t value) {
	mmio_outl(port, value);
}

KERNEL_API
uint32_t ioforge_mmio_inl(uint32_t port) {
	return mmio_inl(port);
}

KERNEL_API
uint16_t ioforge_irq_alloc_entry() {
	auto core_id = coreGetCpuID();
	auto irq = irq_alloc_entry(core_id);
	LOG2_INFO("IOFORGE", "allocating irq on core %d = %d", core_id, irq);
	return irq;
}

KERNEL_API
uint32_t ioforge_isr_get_vector(uint8_t irq) {
	return ioapic_isr_get_vector(irq);
}

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

void KERNEL_API IOforgeStrCopy(char* dst, char* src) {
	strcpy(dst, src);
}

void KERNEL_API IOforgeStrnCopy(char* dst, char* src, size_t len) {
	strncpy(dst, src, len);
}

KERNEL_API
void registerBlockDevice(const char* name, block_device_operations_t* ops,
			 void* identifier) {
	// block_register_device(name, ops, identifier);
}
