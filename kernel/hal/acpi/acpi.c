// #include "hal/acpi/bytestream.h"
#include "hal/apic/apic.h"
#include "hal/apic/ioapic.h"
#include "hal/cpu/paging.h"
#include "hal/pci/pcie.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "libk/io.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include <hal/acpi/acpi.h>
#include <hal/acpi/hpet.h>
#include <libk/str.h>

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

extern void apicSetBaseAddr(uintptr_t addr);

static void vxACPIRegisterNewCore(uint8_t apicid, uint8_t cpuid,
                                  uint32_t flag) {
	struct cpu_core* core =
	    (struct cpu_core*)kalloc(sizeof(struct cpu_core));
	core->apicid = apicid;
	core->cpuid = cpuid;
	core->flag = flag;
	core->next = cpu_list;
	core->status = Off;
	cpu_list = core;
}

uint16_t vxGetNumberOfCores() {
	uint16_t count = 0;
	struct cpu_core* core = cpu_list;
	while (core) {
		count++;
		core = core->next;
	}
	return count;
}

uintptr_t acpi_map_phys_page(uintptr_t phys_addr, size_t len) {
	uintptr_t aligned_phys = ALIGN_DOWN(phys_addr, BLOCK_SIZE);
	uintptr_t offset = phys_addr - aligned_phys;

	uintptr_t vaddr = vma_lookup_free_vaddr(VMA_REGION_A, len);

	vma_register(aligned_phys, vaddr, len * BLOCK_SIZE);

	vxMultipleMmap(paging_get_highest_page_map(), vaddr, aligned_phys, len,
	               PAGE_PRESENT | PAGE_WRITABLE);
	paging_reload(paging_get_highest_page_map());

	return (uintptr_t)(vaddr + offset);
}

void acpi_phys_page_unmap(uintptr_t addr) {
	paging_unmap_fill(paging_get_highest_page_map(), addr, 1);
	paging_reload(paging_get_highest_page_map());
	vma_unregister(addr);
}

static void parsing_dsdt(uintptr_t dsdt_addr) {
	// uintptr_t   ddsdt = acpi_map_phys_page(dsdt_addr, 1);
	// struct SDT *sdt   = (struct SDT *)ddsdt;
	// LOG2_INFO("ACPI", "DSDT header length %d", sdt->Length);
	// uintptr_t new_dsdt = acpi_map_phys_page(dsdt_addr, (BLOCK_SIZE +
	// sdt->Length - 1) / BLOCK_SIZE); acpi_phys_page_unmap(ddsdt); sdt =
	// (struct SDT *)new_dsdt; uint8_t *dsdt = (uint8_t *)new_dsdt;

	// uint32_t slp_typa = 0;
	// uint32_t slp_typb = 0;

	// for (size_t i = 0; i < sdt->Length; i++)
	// {
	//     if (dsdt[i] == NAME_OP)
	//     { // NameOp
	//         if (strncmp((char *)((uintptr_t)dsdt + i + 1), "_S5_", 4) ==
	//         0)
	//         {
	//             LOG2_INFO("ACPI", "found _S5_ at %d", i);
	//             for (size_t j = i + 5; j < sdt->Length; j++)
	//             {
	//                 if (dsdt[j] == PACKAGE_OP)
	//                 {
	//                     LOG2_INFO("ACPI", "found Package at %d", j);

	//                     for (size_t l = j + 1; l < sdt->Length; l++)
	//                     {
	//                         if (dsdt[l] == BYTE_PREFIX)
	//                         {
	//                             LOG2_INFO("ACPI", "found byte prefix
	//                             first at %d", l); break;
	//                         }
	//                     }
	//                     // break;
	//                     break;
	//                 }
	//                 i = j;
	//             }

	//             // break;
	//             // if (*data == 0x12)
	//             // {                               // PackageOp
	//             //     data++;                     // skip opcode
	//             //     size_t  pkg_size = *data++; // asumsi kecil, 1
	//             byte
	//             //     uint8_t num_elem = *data++;
	//             //     // ambil SLP_TYP_A
	//             //     slp_typa = *data; // simplifikasi, bisa 1/2/4
	//             bytes
	//             //     // ambil SLP_TYP_B
	//             //     slp_typb = *(data + 1); // jika ada
	//             // }
	//         }
	//     }
	// }

	// LOG2_INFO("ACPI", "slp_typa %x slptypb %x", slp_typa, slp_typb);
}

static void parsing_fadt(uintptr_t fadt_addr) {
	// struct FADT *fadt = (struct FADT *)fadt_addr;
	// LOG2_INFO("ACPI", "FADT header length %d", fadt->header.Length);
	// LOG2_INFO("ACPI", "FADT PM1a cblk 0x%x", fadt->PM1aControlBlock);
	// LOG2_INFO("ACPI", "FADT  DSDT 0x%x", fadt->Dsdt);
	// parsing_dsdt(fadt->Dsdt);

	// outw(fadt->PM1aControlBlock, (0x1 << 10) | 1);
	// outw(fadt->PM1bControlBlock, (0x1 << 10) | 1);
	// outw(0x604, 0x2000);
}

static void parsing_madt(struct MADT* madt) {
	LOG2_INFO("ACPI", "APIC addr 0x%x", madt->localApicAddress);
	uintptr_t apic_addr = acpi_map_phys_page(madt->localApicAddress, 1);
	apicSetBaseAddr(apic_addr);
	apicInitialize();

	uint8_t* ptr = (uint8_t*)&madt->table;
	uint8_t* ptr_end = (uint8_t*)madt + madt->header.Length;
	LOG2_INFO("ACPI", "madt header 0x%x length 0x%x", ptr, ptr_end);

	// uint8_t bspid = cpuid_get_bsp_id();
	// LOG2_INFO("ACPI", "current bsp id : %d", bspid);

	// madt_record_table_entry_t *a = (madt_record_table_entry_t
	// *)((uintptr_t)madt + 0x2C);

	while (ptr < ptr_end) {
		madt_record_table_entry_t* t = (madt_record_table_entry_t*)ptr;
		// LOG2_INFO("MADT", "type %d, len %d", t->entry_type,
		// t->record_length);
		if (t->record_length == 0)
			break;
		switch (t->entry_type) {
		case ACPI_PROCESSOR_LAPIC: {
			uint8_t apic_id = *(uint8_t*)(ptr + 0x3);
			uint8_t cpu_id = *(uint8_t*)(ptr + 0x2);
			uint32_t flags = *(uint32_t*)(ptr + 0x4);

			LOG2_INFO("ACPI", "found APIC Id %d CPU Id %d", apic_id,
			          cpu_id);
			vxACPIRegisterNewCore(apic_id, cpu_id, flags);

			break;
		}
		case ACPI_IO_INT_OVERRIDE: {
			uint8_t bus_src = *(uint8_t*)(ptr + 0x2);
			uint8_t irq_src = *(uint8_t*)(ptr + 0x3);
			uint32_t gsi = *(uint8_t*)(ptr + 0x4);
			uint16_t flags = *(uint16_t*)(ptr + 0x6);
			LOG_DEBUG("ACPI INT", "BUS %d IRQ %d GSI %d flags %d",
			          bus_src, irq_src, gsi, flags);
			ioapic_add_irq_gsi_map(irq_src, gsi, flags);
			break;
		}
		case ACPI_IO_APIC: {
			uint8_t ioapic_id = *(uint8_t*)(ptr + 0x2);
			uint32_t ioapic_addr = *(uint32_t*)(ptr + 0x4);
			uint32_t ioapic_gsi_base = *(uint32_t*)(ptr + 0x8);
			LOG_DEBUG("ACPIO IOAPIC",
			          "found IOAPIC id %d addr 0x%x GSI Base %d",
			          ioapic_id, ioapic_addr, ioapic_gsi_base);
			ioapic_setup(acpi_map_phys_page(ioapic_addr, 1));
			break;
		}
		case ACPI_NMI: {
			uint8_t processor = *(uint8_t*)(ptr + 0x2);
			uint16_t flags = *(uint16_t*)(ptr + 0x4);
			uint8_t lint = *(uint8_t*)(ptr + 0x6);
			LOG_DEBUG("ACPI NMI", "processor %d flags %d lint %d",
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

INIT(acpi) {
	// todo handle is version 2
	struct RSDP_t* rsdp_ptr =
	    (struct RSDP_t*)acpi_map_phys_page(VIRT2PHYS(ctx->rsdp_addr), 1);

	// Parse RSDP
	// validating checksum
	if (strncmp(rsdp_ptr->Signature, "RSD PTR ", 8) != 0) {
		serial_trace("invalid rsdp signature signature %s\n",
		             rsdp_ptr->Signature);
		return;
	}

	uintptr_t rsdt_addr = rsdp_ptr->RsdtAddress;
	LOG2_INFO("ACPI", "rsdt addr 0x%x", rsdt_addr);

	// Parse RSDT
	struct RSDT* rsdt = (struct RSDT*)acpi_map_phys_page(rsdt_addr, 1);
	LOG2_INFO("RSDT", "rsdt length %d",
	          (rsdt->h.Length - sizeof(rsdt->h)) / 4);

	for (uint64_t i = 0; i < (rsdt->h.Length - sizeof(rsdt->h)) / 4; i++) {
		uintptr_t phys_addr = rsdt->PointerToOtherSDT[i];
		uintptr_t addr = acpi_map_phys_page(phys_addr, 2);
		struct SDT* sdt = (struct SDT*)addr;

		LOG2_INFO("ACPI", "signature %s (%x) found", sdt->Signature,
		          *(uint32_t*)sdt->Signature);

		switch (*(uint32_t*)sdt->Signature) {
		case APIC_SIGNATURE:
			parsing_madt((struct MADT*)addr);
			break;
		case HPET_SIGNATURE:
			vxHPETInitialize(addr);
			break;
		case MCFG_SIGNATURE:
			mcfg_parse(addr);
			break;
		case WAET_SIGNATURE:
			break;
		case FACP_SIGNATURE:
			parsing_fadt(addr);
			break;
		case TPM2_SIGNATURE:
			break;
		default:
			break;
		}
	}
	LOG2_INFO("ACPI", "end search at rsdt");

	// kita tidak butuh lagi setelah semua dibaca
	// acpi_phys_page_unmap((uintptr_t)rsdp_ptr);
}