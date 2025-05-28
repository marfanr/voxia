#include "acpi.h"
#include "madt.h"
#include "rsdp.h"
#include "rsdt.h"
#include <hal/cpu/paging.h>
#include <libk/debug/debug.h>
#include <libk/serial.h>
#include <libk/type.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

cpu_core_t *cpu_list = 0;

struct ACPI_APIC_ENTRY {
  uint8_t type;
  uint8_t length;
} __attribute__((packed));

struct ACPI_IO_APIC {
  struct ACPI_APIC_ENTRY h;
  uint8_t ioApicId;
  uint8_t reserved;
  uint32_t ioApicAddress;
  uint32_t globalSystemInterruptBase;
} __attribute__((packed));

struct ACPI_INT_SRC {
  struct ACPI_APIC_ENTRY h;
  uint8_t busSource;
  uint8_t irqSource;
  uint32_t globalSystemInterrupt;
  uint16_t flags;
} __attribute__((packed));

struct ACPI_NMI {
  struct ACPI_APIC_ENTRY h;
  uint8_t processor;
  uint16_t flags;
  uint8_t lint;
} __attribute__((packed));

uintptr_t local_apic_addr = 0;
uint8_t *io_apic_addr = 0;

int strncmp_(const char *s1, const char *s2, size_t n) {
  while (n-- != 0) {
    if (*s1 != *s2++)
      return *(unsigned char *)s1 - *(unsigned char *)--s2;
    if (*s1++ == 0)
      break;
  }
  return 0;
}

void madt_parse(struct MADT *madt_) {
  KDEBUG(DEBUG_LEVEL_DEBUG,
         "Collecting Information from Multiple APIC Description Table\n");

  uint64_t map = 0xFFFF8FFF00200000;
  paging_mmap_fill((page_t)PHYS2VIRT((uint64_t)paging_get_highest_page_map()),
                   (uint64_t)map, (uint64_t)((madt_->localApicAddress)), 1,
                   0x3);
  paging_reload(paging_get_highest_page_map());
  paging_add_dma_mapping((uint64_t)((madt_->localApicAddress)), map, 1);
  uint64_t offset = madt_->localApicAddress -
                    (madt_->localApicAddress & ~0xFFF); // offset from 0xA0000

  local_apic_addr = map + offset;
  KDEBUG(DEBUG_LEVEL_DEBUG, "Local APIC Address: 0x%x\n", local_apic_addr);

  cpu_list = (cpu_core_t *)(phys_base_alloc(1));

  // parse entries
  uint8_t *ptr = (uint8_t *)&madt_->table;
  size_t ptr_end = (size_t)&madt_->header + madt_->header.length;
  uint8_t bspid, bspdone = 0; // BSP id and spinlock flag
  // get the BSP's Local APIC ID
  __asm__ __volatile__("mov $1, %%eax; cpuid; shrl $24, %%ebx;"
                       : "=b"(bspid)
                       :
                       :);
  serial_trace("bsp id : %d\n", bspid);
  int index = 0;
  while (ptr < ptr_end) {
    switch (*ptr) {
    case 0: {
      KDEBUG(0,
             "FOUND CPU core ID: %d, APIC id: 0x%x, flags: "
             "0b%b\n",
             *(uint8_t *)(ptr + 0x2), (uint8_t)(ptr + 0x3),
             *(uint32_t *)(ptr + 0x4));
      uint8_t apic_id = *(uint8_t *)(ptr + 0x3);
      uint8_t cpu_id = *(uint8_t *)(ptr + 0x2);
      serial_trace("CPU core ID: %d, APIC id: 0x%x, flags: "
                   "0b%b\n",
                   cpu_id, apic_id, *(uint32_t *)(ptr + 0x4));

      cpu_list[index].cpuid = cpu_id;
      cpu_list[index].apicid = apic_id;
      break;
    }
    case 1: {
      struct ACPI_IO_APIC *ioapic = (struct ACPI_IO_APIC *)ptr;
      map += 0x1000;
      paging_mmap_fill(
          (page_t)PHYS2VIRT((uint64_t)paging_get_highest_page_map()),
          (uint64_t)map, (uint64_t)((ioapic->ioApicAddress)), 1, 0x3);
      paging_reload(paging_get_highest_page_map());
      paging_add_dma_mapping((uint64_t)((ioapic->ioApicAddress)), map, 1);

      uint64_t offset =
          ioapic->ioApicAddress - (ioapic->ioApicAddress & ~0xFFF);
      io_apic_addr = (uint8_t *)(map + offset); // 0x1000
      KDEBUG(DEBUG_LEVEL_DEBUG, "IOAPIC: 0x%x\n", io_apic_addr);
      break;
    }
    case 2: {
      struct ACPI_INT_SRC *int_src = (struct ACPI_INT_SRC *)ptr;
      // if (int_src->globalSystemInterrupt == 11)
      //   int_src->busSource = 11;
      KDEBUG(DEBUG_LEVEL_DEBUG, "INTERRUPT SOURCE : %d, INTERRUPT DEST : %d\n",
             int_src->globalSystemInterrupt, int_src->irqSource);
      break;
    }
    case 4: {
      struct ACPI_NMI *nmi = (struct ACPI_NMI *)ptr;
      KDEBUG(DEBUG_LEVEL_DEBUG, "NMI: %d, flags : 0b%b\n", nmi->lint,
             nmi->flags);
      break;
    }
    }
    ptr += *(ptr + 1);
  }
}

void acpi_setup(struct stivale2_struct_tag_rsdp *rsdp_info) {
  // 1. parse RSDP
  ACPI_RSDP *rsdp = (ACPI_RSDP *)rsdp_info->rsdp;
  struct MADT *madt_ = 0;

  // maping rsdt to 0xFFFF800000000000
  uint64_t map = 0xFFFF8FFF00100000;
  paging_mmap_fill((page_t)PHYS2VIRT((uint64_t)paging_get_highest_page_map()),
                   (uint64_t)map, (uint64_t)((rsdp->RsdtAddress)), 3, 0x3);
  paging_reload(paging_get_highest_page_map());
  paging_add_dma_mapping((uint64_t)((rsdp->RsdtAddress)), map, 3);

  uint64_t offset =
      rsdp->RsdtAddress - (rsdp->RsdtAddress & ~0xFFF); // offset from 0xA0000

  // 2. parse RSDT
  struct ACPI_RSDT *rsdt = (struct ACPI_RSDT *)(map + offset);
  KDEBUG(DEBUG_LEVEL_DEBUG, "RSDT Address: 0x%x\n", rsdt);
  serial_trace("RSDT length : %d\n", rsdt->h.Length);

  for (int i = 0; i < (rsdt->h.Length - sizeof(rsdt->h)) / 4; i++) {
    map += 0x1000;
    paging_mmap_fill((page_t)PHYS2VIRT((uint64_t)paging_get_highest_page_map()),
                     (uint64_t)map, (uint64_t)((rsdt->PointerToOtherSDT[i])), 3,
                     0x3);
    paging_reload(paging_get_highest_page_map());
    paging_add_dma_mapping((uint64_t)((rsdt->PointerToOtherSDT[i])), map, 3);
    uint64_t offset =
        rsdt->PointerToOtherSDT[i] - (rsdt->PointerToOtherSDT[i] & ~0xFFF);

    struct ACPI_RSDT *h = (struct ACPI_RSDT *)(map + offset); // 0x1000
    serial_trace("rsdt : %x\n", h);
    if (strncmp_(h->h.Signature, "APIC", 4) == 0) {
      kernel_debug_impl(__FILE__, __LINE__, DEBUG_LEVEL_DEBUG,
                        "MADT Found at: 0x%x\n", h);
      madt_ = (struct MADT *)h;
    }
    if (strncmp_(h->h.Signature, "FACP", 4) == 0) {
      kernel_debug_impl(__FILE__, __LINE__, DEBUG_LEVEL_DEBUG,
                        "FACP Found at: 0x%x\n", h);

      uint16_t *boot_architecture_flags = (uint16_t *)(h + 109);
      kernel_debug_impl(__FILE__, __LINE__, DEBUG_LEVEL_DEBUG,
                        "Boot Architecture Flags: 0b%b\n",
                        *boot_architecture_flags);
      // madt_ = (struct MADT *)h;
    }
    if (strncmp_(h->h.Signature, "HPET", 4) == 0) {
      serial_trace("HPET Found at: 0x%x \n", h);
    }
  }

  if (!madt_) {
    KDEBUG(DEBUG_LEVEL_ERROR, "Multiple APIC Description Table not found\n");
    for (;;)
      ;
  }

  KDEBUG(DEBUG_LEVEL_DEBUG, "MADT Address: 0x%x\n", madt_);

  // 3. Parse APIC
  madt_parse(madt_);
  KDEBUG(DEBUG_LEVEL_DEBUG, "ACPI Initialized\n");
}
