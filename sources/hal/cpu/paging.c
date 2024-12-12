#include "paging.h"
#include <libk/serial.h>
#include <libk/str/memset.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

page_t paging_create_page_directory() {
  page_t page = (page_t)VIRT2PHYS((uintptr_t)phys_base_alloc(1));
  // serial_trace("Page directory: 0x%x\n", ((uint64_t)page));
  memset((void *)(uintptr_t)((uint64_t)page), 0, VMM_PAGE_SIZE);
  return page;
}

page_t pml4;

void paging_install() {
  serial_trace("Installing paging\n");
  pml4 = VMM_PAGE;
  serial_trace("PML4: 0x%x\n", ((uint64_t)pml4));

  paging_setup(pml4);

  // mapping serial port 3 to user space
  for (uint64_t i = 0; i < 0x1000; i += 0x1000) {
    paging_mmap(pml4, 0x3f8 + i, 0x3f8 + i, 0b111);
  }

  paging_reload(((uint64_t)pml4));

  serial_send_string("Page Map Level 4 directory: ");
  serial_send_number(((uint64_t)(pml4)), 16);
  serial_send_string("\n");
}

void paging_fork(page_t parent_pml4, page_t child_pml4) {
  for (uint64_t i = 0; i < 512; i++) {
    if (parent_pml4[i] & 1) {
      page_t pdpt = (page_t)(parent_pml4[i] & ~(511));
      page_t child_pdpt = (page_t)VIRT2PHYS((uintptr_t)phys_base_alloc(1));
      child_pml4[i] = ((uint64_t)child_pdpt);

      for (uint64_t j = 0; j < 512; j++) {
        if (pdpt[j] & 1) {
          page_t pdp = (page_t)(pdpt[j] & ~(511));
          page_t child_pdp = (page_t)VIRT2PHYS((uintptr_t)phys_base_alloc(1));
          child_pdpt[j] = ((uint64_t)child_pdp);

          for (uint64_t k = 0; k < 512; k++) {
            if (pdp[k] & 1) {
              page_t pt = (page_t)(pdp[k] & ~(511));
              page_t child_pt =
                  (page_t)VIRT2PHYS((uintptr_t)phys_base_alloc(1));
              child_pdp[k] = ((uint64_t)child_pt);

              for (uint64_t l = 0; l < 512; l++) {
                if (pt[l] & 1) {
                  uint64_t phys = pt[l] & ~(511);
                  uint64_t virt =
                      (uint64_t)VIRT2PHYS((uintptr_t)phys_base_alloc(1));
                  child_pt[l] = virt;

                  for (uint64_t m = 0; m < 0x1000; m++) {
                    *(uint8_t *)(virt + m) = *(uint8_t *)(phys + m);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void paging_setup(page_t pml4) {
  for (uint64_t i = 0; i < 4 * GB; i += 0x1000) {
    paging_mmap(pml4, PHYS2VIRT(i), i, 0b11);
  }

  for (uint64_t i = 0; i < 4 * GB; i += 0x1000) {
    paging_mmap(pml4, i, i, 0b111);
  }

  for (uint64_t i = 0; i < 0x80000000; i += 0x1000) {
    paging_mmap(pml4, (uint64_t)i + 0xffffffff80000000, i, 0b11);
  }
}

void paging_mmap(page_t page_dir, uint64_t virt, uint64_t phys, int flags) {
  uint64_t index4 = (virt >> 39) & 0x1ff;
  uint64_t index3 = (virt >> 30) & 0x1ff;
  uint64_t index2 = (virt >> 21) & 0x1ff;
  uint64_t index1 = (virt >> 12) & 0x1ff;

  page_t pml4 = page_dir;
  page_t pdpt = 0;
  page_t pdp = 0;
  page_t pt = 0;

  if (pml4[index4] & 1) {
    pdpt = (page_t)(pml4[index4] & ~(511));
  } else {
    pdpt = VMM_PAGE;
    pml4[index4] = ((uint64_t)pdpt) | flags;
  }

  if (pdpt[index3] & 1) {
    pdp = (page_t)(pdpt[index3] & ~(511));
  } else {
    pdp = (page_t)VMM_PAGE;
    pdpt[index3] = ((uint64_t)pdp) | flags;
  }

  if (pdp[index2] & 1) {
    pt = (page_t)(pdp[index2] & ~(511));
  } else {
    pt = (page_t)VMM_PAGE;
    pdp[index2] = ((uint64_t)pt) | flags;
  }

  pt[index1] = phys | flags;

  asm volatile("invlpg (%0)" ::"r"(virt)
               : "memory"); // flush the entry from TLB
}

void paging_unmap_page(page_t page_dir, uint64_t virt) {
  uint64_t index4 = (virt >> 39) & 0x1ff;
  uint64_t index3 = (virt >> 30) & 0x1ff;
  uint64_t index2 = (virt >> 21) & 0x1ff;
  uint64_t index1 = (virt >> 12) & 0x1ff;

  page_t pml4 = page_dir;
  page_t pdpt = 0;
  page_t pdp = 0;
  page_t pt = 0;

  if (pml4[index4] & 1) {
    pdpt = (page_t)(pml4[index4] & ~(511));
  } else {
    return;
  }

  if (pdpt[index3] & 1) {
    pdp = (page_t)(pdpt[index3] & ~(511));
  } else {
    return;
  }

  if (pdp[index2] & 1) {
    pt = (page_t)(pdp[index2] & ~(511));
  } else {
    return;
  }

  pt[index1] = 0;
  asm volatile("invlpg (%0)" ::"r"(virt)
               : "memory"); // flush the entry from TLB
}

void paging_reload(page_t pml4) {
  asm volatile("mov %0, %%cr3" ::"r"((uint64_t)pml4) : "memory");
}

page_t paging_get_highest_page_map(void) { return pml4; }