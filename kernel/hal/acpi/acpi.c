// #include "hal/acpi/bytestream.h"
#include "hal/apic/apic.h"
#include "hal/apic/ioapic.h"
#include "hal/cpu/cpuid.h"
#include "hal/cpu/paging.h"
#include "hal/pci/pcie.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "libk/io.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "type.h"
#include <hal/acpi/acpi.h>
#include <hal/acpi/hpet.h>
#include <str.h>
#include <type.h>

static struct cpu_core* cpu_list = 0;

struct cpu_core* vxGetCpuInfo(uint8_t apicid) {
	struct cpu_core* core = cpu_list;
	while (core) {
		if (core->apicid == apicid)
			return core;
		core = core->next;
	}
	return 0;
}

struct cpu_core* vxGetCpuInfoByIndex(uint8_t index) {
	struct cpu_core* core = cpu_list;
	uint8_t curr = 0;
	while (core) {
		if (curr == index)
			return core;
		curr++;
		core = core->next;
	}
	return 0;
}

extern void apicSetBaseAddr(uintptr_t addr);

__attribute__((unused)) static void
vxACPIRegisterNewCore(uint8_t apicid, uint8_t cpuid, uint32_t flag) {
	struct cpu_core* core =
	    (struct cpu_core*)kalloc(sizeof(struct cpu_core));
	core->apicid = apicid;
	core->cpuid = cpuid;
	core->flag = flag;
	core->next = cpu_list;
	core->status = Off;
	cpu_list = core;
}

uint8_t vxGetNumberOfCores() {
	uint8_t count = 0;
	struct cpu_core* core = cpu_list;
	while (core) {
		count++;
		core = core->next;
	}
	return count;
}

uintptr_t acpi_map_phys_page(uintptr_t phys_addr, size_t size_in_bytes) {
	uintptr_t aligned_phys = ALIGN_DOWN(phys_addr, BLOCK_SIZE);
	uintptr_t offset = phys_addr - aligned_phys;
	size_t len_in_4kb =
	    ALIGN_UP(offset + size_in_bytes, BLOCK_SIZE) / BLOCK_SIZE;

	uintptr_t vaddr = vma_lookup_free_vaddr(get_kernel_vmm_page(),
	                                        VMA_REGION_A, len_in_4kb);
	if (!vaddr) {
		serial2_printf("acpi_map_phys_page: failed to find free vaddr "
		             "for size %lu\n",
		             size_in_bytes);
		return 0;
	}

	vma_register(get_kernel_vmm_page(), aligned_phys, vaddr,
	             len_in_4kb * BLOCK_SIZE,
	             PAGE_PRESENT | PAGE_WRITABLE | PAGE_NO_EXECUTE |
	                 PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE);

	paging_multiple_mmap(paging_get_highest_page_map(), vaddr, aligned_phys,
	                     len_in_4kb,
	                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_NO_EXECUTE |
	                         PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE);

	for (size_t i = 0; i < len_in_4kb; i++) {
		INVLPG(vaddr + i * BLOCK_SIZE);
	}
	asm volatile("mfence" ::: "memory");

	serial2_printf("map_phys: 0x%lx -> 0x%lx (size %lu)\n", phys_addr,
	               vaddr + offset, size_in_bytes);

	return (uintptr_t)(vaddr + offset);
}

void acpi_phys_page_unmap(uintptr_t addr, size_t size_in_bytes) {
	uintptr_t aligned_vaddr = ALIGN_DOWN(addr, BLOCK_SIZE);
	uintptr_t offset = addr - aligned_vaddr;
	size_t len_in_4kb =
	    ALIGN_UP(offset + size_in_bytes, BLOCK_SIZE) / BLOCK_SIZE;

	paging_multiple_unmap(paging_get_highest_page_map(), aligned_vaddr,
	                      len_in_4kb);
	vma_unregister(get_kernel_vmm_page(), aligned_vaddr);
}

__attribute__((unused)) static void parsing_madt(struct MADT* madt) {
	LOG2_INFO("ACPI", "APIC addr 0x%lx", (uintptr_t)madt->localApicAddress);
	uintptr_t apic_addr =
	    acpi_map_phys_page(madt->localApicAddress, 0x1000);
	apicSetBaseAddr(apic_addr);
	apicInitialize();

	uint8_t* ptr = (uint8_t*)&madt->table;
	uint8_t* ptr_end = (uint8_t*)madt + madt->header.Length;
	// LOG2_INFO("ACPI", "madt header 0x%lx length 0x%x", (uintptr_t)ptr,
	// madt->header.Length);

	while (ptr < ptr_end) {
		madt_record_table_entry_t* t = (madt_record_table_entry_t*)ptr;
		if (t->record_length == 0)
			break;
		switch (t->entry_type) {
		case ACPI_PROCESSOR_LAPIC: {
			uint8_t apic_id = *(uint8_t*)(void*)(ptr + 0x3);
			uint8_t cpu_id = *(uint8_t*)(void*)(ptr + 0x2);
			uint32_t flags = *(uint32_t*)(void*)(ptr + 0x4);

			LOG2_INFO("ACPI", "found APIC Id %d CPU Id %d", apic_id,
			         cpu_id);
			vxACPIRegisterNewCore(apic_id, cpu_id, flags);

			break;
		}
		case ACPI_IO_INT_OVERRIDE: {
			uint8_t bus_src = *(uint8_t*)(void*)(ptr + 0x2);
			uint8_t irq_src = *(uint8_t*)(void*)(ptr + 0x3);
			uint32_t gsi = *(uint8_t*)(void*)(ptr + 0x4);
			uint16_t flags = *(uint16_t*)(void*)(ptr + 0x6);
			LOG2_DEBUG("ACPI INT", "BUS %d IRQ %d GSI %d flags %d",
			          bus_src, irq_src, gsi, flags);
			ioapic_add_irq_gsi_map(irq_src, gsi, flags);
			break;
		}
		case ACPI_IO_APIC: {
			uint8_t ioapic_id = *(uint8_t*)(void*)(ptr + 0x2);
			uint32_t ioapic_addr = *(uint32_t*)(void*)(ptr + 0x4);
			uint32_t ioapic_gsi_base =
			    *(uint32_t*)(void*)(ptr + 0x8);
			LOG2_DEBUG("ACPIO IOAPIC",
			          "found IOAPIC id %d addr 0x%x GSI Base %d",
			          ioapic_id, ioapic_addr, ioapic_gsi_base);
			ioapic_setup(acpi_map_phys_page(ioapic_addr, 0x1000));
			break;
		}
		case ACPI_NMI: {
			uint8_t processor = *(uint8_t*)(void*)(ptr + 0x2);
			uint16_t flags = *(uint16_t*)(void*)(ptr + 0x4);
			uint8_t lint = *(uint8_t*)(void*)(ptr + 0x6);
			LOG2_DEBUG("ACPI NMI", "processor %d flags %d lint %d",
			          processor, flags, lint);

			break;
		}
		}
		ptr += t->record_length;
	}
}

#define APIC_SIGNATURE 0x43495041
#define HPET_SIGNATURE 0x54455048
#define MCFG_SIGNATURE 0x4746434D
#define WAET_SIGNATURE 0x54454157
#define FACP_SIGNATURE 0x50434146
#define TPM2_SIGNATURE 0x324D5054
#define DSDT_SIGNATURE 0x54445344

// TODO: will be handled by ACPICA
INIT(acpi) {
	// Map RSDP (typically 36 bytes for v2)
	struct RSDP_t* rsdp_ptr =
	    (struct RSDP_t*)acpi_map_phys_page(VIRT2PHYS(ctx->rsdp_addr), 40);

	if (!rsdp_ptr || (uintptr_t)rsdp_ptr < 0x1000) {
		serial2_printf("failed to map RSDP (ptr: 0x%lx)\n",
		             (uintptr_t)rsdp_ptr);
		return;
	}

	if (strncmp(rsdp_ptr->Signature, "RSD PTR ", 8) != 0) {
		serial2_printf("invalid rsdp signature: %.8s\n",
		             rsdp_ptr->Signature);
		return;
	}

	uintptr_t rsdt_phys = rsdp_ptr->RsdtAddress;
	LOG2_INFO("ACPI", "rsdt phys addr 0x%lx", rsdt_phys);

	// First map only the header to get the length
	struct RSDT* rsdt_header =
	    (struct RSDT*)acpi_map_phys_page(rsdt_phys, sizeof(struct SDT));
	if (!rsdt_header || (uintptr_t)rsdt_header < 0x1000) {
		serial2_printf("failed to map RSDT header\n");
		return;
	}

	uint32_t rsdt_len = rsdt_header->h.Length;
	acpi_phys_page_unmap((uintptr_t)rsdt_header, sizeof(struct SDT));

	// Now map the FULL RSDT
	struct RSDT* rsdt =
	    (struct RSDT*)acpi_map_phys_page(rsdt_phys, rsdt_len);
	if (!rsdt) {
		serial2_printf("failed to map full RSDT\n");
		return;
	}

	uint32_t entry_count = (rsdt->h.Length - sizeof(rsdt->h)) / 4;
	LOG2_INFO("RSDT", "rsdt entry count %d (total length %d)", entry_count,
	         rsdt_len);

	for (uint32_t i = 0; i < entry_count; i++) {
		uintptr_t sdt_phys = rsdt->PointerToOtherSDT[i];

		// Map header first
		struct SDT* sdt_header = (struct SDT*)acpi_map_phys_page(
		    sdt_phys, sizeof(struct SDT));
		if (!sdt_header)
			continue;

		uint32_t sdt_len = sdt_header->Length;
		acpi_phys_page_unmap((uintptr_t)sdt_header, sizeof(struct SDT));

		// Map FULL SDT
		struct SDT* sdt =
		    (struct SDT*)acpi_map_phys_page(sdt_phys, sdt_len);
		if (!sdt)
			continue;

		uint32_t sig = *(uint32_t*)(void*)(sdt->Signature);
		LOG2_INFO("ACPI", "signature %.4s (0x%x) found", sdt->Signature,
		         sig);

		switch (sig) {
		case APIC_SIGNATURE:
			parsing_madt((struct MADT*)sdt);
			break;
		case HPET_SIGNATURE:
			vxHPETInitialize((uintptr_t)sdt);
			break;
		case MCFG_SIGNATURE:
			mcfg_parse((uintptr_t)sdt);
			break;
		default:
			break;
		}

		// We can unmap it now if we don't need it anymore,
		// but MADT might be needed by others. Let's keep for now or
		// unmap later.
	}

	LOG2_INFO("ACPI", "end search at rsdt");
}