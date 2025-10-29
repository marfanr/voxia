#include "hal/apic/apic.h"
#include "hal/apic/ioapic.h"
#include "hal/cpu/paging.h"
#include "hal/pci/pcie.h"
#include "init/init.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include <hal/acpi/acpi.h>
#include <hal/acpi/hpet.h>
#include <libk/str.h>

struct cpu_core *cpu_list = 0;

static void
add_core(uint8_t apicid, uint8_t cpuid, uint32_t flag)
{
    struct cpu_core *core = (struct cpu_core *)kalloc(sizeof(struct cpu_core));
    core->apicid          = apicid;
    core->cpuid           = cpuid;
    core->flag            = flag;
    core->next            = cpu_list;
    cpu_list              = core;
}

uintptr_t
acpi_map_phys_page(uintptr_t phys_addr, size_t len)
{
    uintptr_t aligned_phys = ALIGN_DOWN(phys_addr, BLOCK_SIZE);
    uintptr_t offset       = phys_addr - aligned_phys;

    uintptr_t vaddr = vma_lookup_free_vaddr(VMA_REGION_A, len);

    vma_register(aligned_phys, vaddr, len * BLOCK_SIZE);

    paging_mmap_fill(paging_get_highest_page_map(), vaddr, aligned_phys, len,
                     PAGE_PRESENT | PAGE_WRITABLE);
    paging_reload(paging_get_highest_page_map());

    return (uintptr_t)(vaddr + offset);
}

void
acpi_phys_page_unmap(uintptr_t addr)
{
    paging_unmap_fill(paging_get_highest_page_map(), addr, 1);
    paging_reload(paging_get_highest_page_map());
    vma_unregister(addr);
}

static void
parsing_madt(struct MADT *madt)
{
    LOG_INFO("ACPI", "APIC addr 0x%x", madt->localApicAddress);
    apic_initialize(acpi_map_phys_page(madt->localApicAddress, 1));

    uint8_t *ptr     = (uint8_t *)&madt->table;
    uint8_t *ptr_end = (uint8_t *)madt + madt->header.Length;
    LOG_INFO("ACPI", "madt header 0x%x length 0x%x", ptr, ptr_end);

    // uint8_t bspid = cpuid_get_bsp_id();
    // LOG_INFO("ACPI", "current bsp id : %d", bspid);

    madt_record_table_entry_t *a = (madt_record_table_entry_t *)((uintptr_t)madt + 0x2C);
    // LOG_INFO("MADT", " a type %d, len %d", a->entry_type, a->record_length);

    while (ptr < ptr_end)
    {
        madt_record_table_entry_t *t = (madt_record_table_entry_t *)ptr;
        // LOG_INFO("MADT", "type %d, len %d", t->entry_type, t->record_length);
        if (t->record_length == 0)
            break;
        switch (t->entry_type)
        {
            case ACPI_PROCESSOR_LAPIC:
            {
                uint8_t  apic_id = *(uint8_t *)(ptr + 0x3);
                uint8_t  cpu_id  = *(uint8_t *)(ptr + 0x2);
                uint32_t flags   = *(uint32_t *)(ptr + 0x4);

                LOG_INFO("ACPI", "found APIC Id %d CPU Id %d", apic_id, cpu_id);
                add_core(apic_id, cpu_id, flags);

                break;
            }
            case ACPI_IO_INT_OVERRIDE:
            {
                uint8_t  bus_src = *(uint8_t *)(ptr + 0x2);
                uint8_t  irq_src = *(uint8_t *)(ptr + 0x3);
                uint32_t gsi     = *(uint8_t *)(ptr + 0x4);
                uint16_t flags   = *(uint16_t *)(ptr + 0x6);
                LOG_DEBUG("ACPI INT", "BUS %d IRQ %d GSI %d flags %d", bus_src, irq_src, gsi,
                          flags);
                ioapic_add_irq_gsi_map(irq_src, gsi, flags);
                break;
            }
            case ACPI_IO_APIC:
            {
                uint8_t  ioapic_id       = *(uint8_t *)(ptr + 0x2);
                uint32_t ioapic_addr     = *(uint32_t *)(ptr + 0x4);
                uint32_t ioapic_gsi_base = *(uint32_t *)(ptr + 0x8);
                LOG_DEBUG("ACPIO IOAPIC", "found IOAPIC id %d addr 0x%x GSI Base %d", ioapic_id,
                          ioapic_addr, ioapic_gsi_base);
                ioapic_setup(acpi_map_phys_page(ioapic_addr, 1));
                break;
            }
            case ACPI_NMI:
            {
                uint8_t  processor = *(uint8_t *)(ptr + 0x2);
                uint16_t flags     = *(uint16_t *)(ptr + 0x4);
                uint8_t  lint      = *(uint8_t *)(ptr + 0x6);
                LOG_DEBUG("ACPI NMI", "processor %d flags %d lint %d", processor, flags, lint);

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

INIT(acpi)
{
    // todo handle is version 2
    struct RSDP_t *rsdp_ptr = (struct RSDP_t *)acpi_map_phys_page(VIRT2PHYS(ctx->rsdp_addr), 1);

    // Parse RSDP
    // validating checksum
    if (strncmp(rsdp_ptr->Signature, "RSD PTR ", 8) != 0)
    {
        serial_trace("invalid rsdp signature signature %s\n", rsdp_ptr->Signature);
        return;
    }

    uintptr_t rsdt_addr = rsdp_ptr->RsdtAddress;
    LOG_INFO("ACPI", "rsdt addr 0x%x", rsdt_addr);

    // Parse RSDT
    struct RSDT *rsdt = (struct RSDT *)acpi_map_phys_page(rsdt_addr, 1);
    LOG_INFO("RSDT", "rsdt length %d", (rsdt->h.Length - sizeof(rsdt->h)) / 4);

    for (uint64_t i = 0; i < (rsdt->h.Length - sizeof(rsdt->h)) / 4; i++)
    {
        uintptr_t   phys_addr = rsdt->PointerToOtherSDT[i];
        uintptr_t   addr      = acpi_map_phys_page(phys_addr, 2);
        struct SDT *sdt       = (struct SDT *)addr;

        LOG_INFO("ACPI", "signature %s (%x) found", sdt->Signature, *(uint32_t *)sdt->Signature);

        switch (*(uint32_t *)sdt->Signature)
        {
            case APIC_SIGNATURE:
                parsing_madt((struct MADT *)addr);
                break;
            case HPET_SIGNATURE:
                hpet_initialize(addr);
                break;
            case MCFG_SIGNATURE:
                mcfg_parse(addr);
                break;
            case WAET_SIGNATURE:
                break;
            case FACP_SIGNATURE:
                break;
            case TPM2_SIGNATURE:
                break;
            default:
                break;
        }
    }
    LOG_INFO("ACPI", "end search at rsdt");

    // kita tidak butuh lagi setelah semua dibaca
    acpi_phys_page_unmap((uintptr_t)rsdp_ptr);
}