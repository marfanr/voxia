#include "paging.h"
#include "libk/type.h"
#include <config.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/phys_window.h>
#include <memory/vm_manager.h>

#define PHYS_BASE_METADATA_ADDR 0xffffffe000000000

boolean_t paging_has_been_set = false;

extern uint8_t *bitmap_base_;
extern uint64_t metadata_size;

typedef struct paging_page paging_page;
struct paging_page
{
    uintptr_t    page;
    paging_page *next;
} __attribute__((aligned(32)));

page_t
paging_create_page_directory()
{
    page_t    page                    = (page_t)phys_base_alloc(1);
    uintptr_t virtual_physwindow_addr = (uintptr_t)page;

    if (paging_has_been_set)
    {
        mem_create_physwindow((uintptr_t)page, &virtual_physwindow_addr, 0);
    }

    memset((void *)virtual_physwindow_addr, 0, VMM_PAGE_SIZE);
    return page;
}

page_t                      pml4;
static dma_memory_mapping_t mapping_data[CONFIG_PAGING_MAX_MAPPED_DATA_COUNT];
static int                  mapping_data_count = 0;
static page_t               physwindow_pt      = 0;

void
paging_add_dma_mapping(uintptr_t phys, uintptr_t virt, uint64_t size)
{
    mapping_data[mapping_data_count].phys = phys;
    mapping_data[mapping_data_count].virt = virt;
    mapping_data[mapping_data_count].size = size;
    mapping_data[mapping_data_count].next = 0;
    mapping_data_count++;
}

void
paging_physwindow_mmap(page_t page_dir, uint64_t virt, uint64_t phys, int flags)
{
    uint64_t index1 = (virt >> 12) & 0x1ff;
    if (!physwindow_pt)
    {
        uint64_t index4 = (virt >> 39) & 0x1ff;
        uint64_t index3 = (virt >> 30) & 0x1ff;
        uint64_t index2 = (virt >> 21) & 0x1ff;

        page_t pml4 = page_dir;
        page_t pdpt = 0;
        page_t pdp  = 0;

        pdpt         = (page_t)VMM_PAGE;
        pml4[index4] = (uint64_t)pdpt | flags;

        pdp          = (page_t)VMM_PAGE;
        pdpt[index3] = (uint64_t)pdp | flags;

        physwindow_pt = (page_t)VMM_PAGE;
        pdp[index2]   = (uint64_t)physwindow_pt | flags;
    }
    physwindow_pt[index1] = phys | flags;

    asm volatile("invlpg (%0)" ::"r"(virt) : "memory"); // flush the entry from TLB
}

static void
initialize_physical_paging_window()
{

    for (uint64_t i = 0; i < CONFIG_PHYS_MAX_WINDOW_COUNT; i++)
    {
        paging_physwindow_mmap(pml4, (uint64_t)(mem_vma_phys_window_start + i * 0x1000), 0, 0b1);
    }

    // mapping phsywindow_pt
    paging_mmap_fill(pml4, mem_vma_phys_window_pt, (uint64_t)physwindow_pt, 511, 0b11);

    LOG_INFO("PAGING", "mapping physwindow_pt 0x%x to 0x%x", (uint64_t)physwindow_pt,
             (uint64_t)mem_vma_phys_window_pt);
    physwindow_pt = (page_t)mem_vma_phys_window_pt;
}

void
paging_install()
{
    pml4 = VMM_PAGE;
    LOG_INFO("PAGING", "PML4: 0x%", ((uint64_t)pml4));

    paging_setup(pml4);

    LOG_INFO("PAGING", "dma mapping count %", mapping_data_count);

    initialize_physical_paging_window();

    paging_mmap_fill(pml4, PHYS_BASE_METADATA_ADDR, (uint64_t)bitmap_base_,
                     metadata_size / VMM_PAGE_SIZE, 0b11);
    paging_reload(pml4);
    LOG_INFO("PAGING", "paging setup done");
    bitmap_base_ = (uint8_t *)PHYS_BASE_METADATA_ADDR;

    paging_has_been_set = 1;
}

void
paging_fork(page_t parent_pml4, page_t child_pml4)
{
    for (uint64_t i = 0; i < 512; i++)
    {
        if (parent_pml4[i] & 1)
        {
            child_pml4[i] = parent_pml4[i];
        }
    }
}

extern void __r();

void
paging_debug(page_t pml4, uint64_t virt)
{
    serial_trace("Debugging virtual address: 0x%x\n", virt);

    uint64_t index4 = (virt >> 39) & 0x1ff;
    uint64_t index3 = (virt >> 30) & 0x1ff;
    uint64_t index2 = (virt >> 21) & 0x1ff;
    uint64_t index1 = (virt >> 12) & 0x1ff;

    page_t pdpt = 0;
    page_t pdp  = 0;
    page_t pt   = 0;

    serial_trace("PML4: 0x%x\n", pml4);

    if (!(pml4[index4] & 1))
    {
        serial_trace("PML4 entry not present\n\n");
        return;
    }

    pdpt = (page_t)PHYS2VIRT((uint64_t)(pml4[index4] & ~(0xFFF)));
    serial_trace("PDPT: 0x%x\n", VIRT2PHYS(pdpt));
    serial_trace("flags 0b%b\n", pml4[index4] & (0xFFF));

    if (!(pdpt[index3] & 1))
    {
        serial_trace("PDPT entry not present\n\n");
        return;
    }

    pdp = (page_t)PHYS2VIRT((uint64_t)(pdpt[index3] & ~(0xFFF)));
    serial_trace("PDP: 0x%x\n", VIRT2PHYS(pdp));
    serial_trace("flags 0b%b\n", pdpt[index3] & (0xFFF));

    if (!(pdp[index2] & 1))
    {
        serial_trace("PDP entry not present\n\n");
        return;
    }

    pt = (page_t)PHYS2VIRT((uint64_t)(pdp[index2] & ~(0xFFF)));
    serial_trace("PT: 0x%x\n", VIRT2PHYS(pt));
    serial_trace("flags 0b%b\n", pdp[index2] & (0xFFF));

    if (!(pt[index1] & 1))
    {
        serial_trace("PT entry not present\n\n");
        return;
    }

    serial_trace("Page: 0x%x\n", pt[index1] & ~(0xFFF));
    serial_trace("flags 0b%b\n", pt[index1] & (0xFFF));
    serial_send_string("\n");
}

// depend on bootloader
void
paging_setup(page_t pml4)
{

    // TODO: stop using  phys2virt
    //  for (uint64_t i = 0; i < 4 * GB; i += 0x1000) {
    //  paging_mmap(pml4, PHYS2VIRT(i), i, 0b11);
    //  }

    for (uint64_t i = 0; i < 0x80000000; i += 0x1000)
    {
        paging_mmap(pml4, (uint64_t)i + 0xffffffff80000000, i, 0b11);
    }

    for (int i = 0; i < mapping_data_count; i++)
    {
        paging_mmap_fill(pml4, mapping_data[i].virt, mapping_data[i].phys, mapping_data[i].size,
                         0b111);
    }

    //  mapping 0x0240000000 to __r
    void *a = (void *)(phys_base_alloc(1 + 0x1000 / 4096));
    memcopy(a, (void *)__r, 0x1000);
    paging_mmap(pml4, 0x0240000000, VIRT2PHYS((uint64_t)a), 0b1);
}

void
paging_mmap(page_t page_dir, uint64_t virt, uint64_t phys, int flags)
{
    uint64_t index4 = (virt >> 39) & 0x1ff;
    uint64_t index3 = (virt >> 30) & 0x1ff;
    uint64_t index2 = (virt >> 21) & 0x1ff;
    uint64_t index1 = (virt >> 12) & 0x1ff;

    page_t pml4 = page_dir;
    page_t pdpt = 0;
    page_t pdp  = 0;
    page_t pt   = 0;

    uintptr_t pml4_virt_addr = (uintptr_t)pml4;
    if (paging_has_been_set)
    {
        mem_create_physwindow((uintptr_t)pml4, &pml4_virt_addr,
                              PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
                                  PHYS_WINDOW_FLAG_LOCK);
    }
    pml4 = (page_t)pml4_virt_addr;

    if (pml4[index4] & 1)
    {
        uintptr_t pdpt_phys_addr = pml4[index4] & ~(0xFFF);
        uintptr_t pdpt_virt_addr = pdpt_phys_addr;

        if (paging_has_been_set)
        {
            mem_create_physwindow(pdpt_phys_addr, &pdpt_virt_addr,
                                  PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
                                      PHYS_WINDOW_FLAG_LOCK);
        }
        pdpt         = (page_t)pdpt_virt_addr;
        pml4[index4] = (uint64_t)pdpt_phys_addr | flags;
    }
    else
    {
        uintptr_t pdpt_phys_addr = (uintptr_t)phys_base_alloc(1);
        uintptr_t pdpt_virt_addr = pdpt_phys_addr;
        if (paging_has_been_set)
        {
            mem_create_physwindow(pdpt_phys_addr, &pdpt_virt_addr,
                                  PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
                                      PHYS_WINDOW_FLAG_LOCK);
        }
        memset((void *)pdpt_virt_addr, 0, VMM_PAGE_SIZE);
        pdpt         = (page_t)pdpt_virt_addr;
        pml4[index4] = (uint64_t)pdpt_phys_addr | flags;
    }

    if (pdpt[index3] & 1)
    {
        uintptr_t pdp_phys_addr = pdpt[index3] & ~(0xFFF);
        uintptr_t pdp_virt_addr = pdp_phys_addr;

        if (paging_has_been_set)
        {
            mem_create_physwindow(pdp_phys_addr, &pdp_virt_addr,
                                  PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
                                      PHYS_WINDOW_FLAG_LOCK);
        }
        pdp          = (page_t)pdp_virt_addr;
        pdpt[index3] = (uint64_t)pdp_phys_addr | flags;
    }
    else
    {
        uintptr_t pdp_phys_addr = (uintptr_t)phys_base_alloc(1);
        uintptr_t pdp_virt_addr = pdp_phys_addr;
        if (paging_has_been_set)
        {
            mem_create_physwindow(pdp_phys_addr, &pdp_virt_addr,
                                  PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
                                      PHYS_WINDOW_FLAG_LOCK);
        }
        memset((void *)pdp_virt_addr, 0, VMM_PAGE_SIZE);
        pdp          = (page_t)pdp_virt_addr;
        pdpt[index3] = (uint64_t)pdp_phys_addr | flags;
    }

    if (pdp[index2] & 1)
    {
        uintptr_t pt_phys_addr = pdp[index2] & ~(0xFFF);
        uintptr_t pt_virt_addr = pt_phys_addr;
        if (paging_has_been_set)
        {
            mem_create_physwindow(pt_phys_addr, &pt_virt_addr, 0);
        }

        pt          = (page_t)pt_virt_addr;
        pdp[index2] = (uint64_t)pt_phys_addr | flags;
    }
    else
    {
        uintptr_t pt_phys_addr = (uintptr_t)phys_base_alloc(1);
        uintptr_t pt_virt_addr = pt_phys_addr;
        if (paging_has_been_set)
        {
            mem_create_physwindow(pt_phys_addr, &pt_virt_addr,
                                  PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
                                      PHYS_WINDOW_FLAG_LOCK);
        }
        memset((void *)pt_virt_addr, 0, VMM_PAGE_SIZE);
        pt = (page_t)pt_virt_addr;

        pdp[index2] = (uint64_t)pt_phys_addr | flags;
    }

    pt[index1] = phys | flags;

    if (paging_has_been_set)
    {
        mem_release_physwindow((uintptr_t)pml4);
        mem_release_physwindow((uintptr_t)pdpt);
        mem_release_physwindow((uintptr_t)pdp);
        mem_release_physwindow((uintptr_t)pt);
    }
    asm volatile("invlpg (%0)" ::"r"(virt) : "memory"); // flush the entry from TLB
}

void
paging_mmap_fill(page_t page_dir, uint64_t virt, uint64_t phys, uint64_t size, int flags)
{
    for (uint64_t i = 0; i < size; i++)
    {
        paging_mmap(page_dir, virt + i * 4096, phys + i * 4096, flags);
    }
}

void
paging_unmap_page(page_t page_dir, uint64_t virt)
{
    uint64_t index4 = (virt >> 39) & 0x1ff;
    uint64_t index3 = (virt >> 30) & 0x1ff;
    uint64_t index2 = (virt >> 21) & 0x1ff;
    uint64_t index1 = (virt >> 12) & 0x1ff;

    page_t pml4 = page_dir;
    page_t pdpt = 0;
    page_t pdp  = 0;
    page_t pt   = 0;

    uintptr_t pml4_virt_addr = (uintptr_t)pml4;
    if (paging_has_been_set)
    {
        mem_create_physwindow((uintptr_t)pml4, &pml4_virt_addr,
                              PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
                                  PHYS_WINDOW_FLAG_LOCK);
    }
    pml4 = (page_t)pml4_virt_addr;

    if (pml4[index4] & 1)
    {
        uintptr_t pdpt_phys_addr = pml4[index4] & ~(0xFFF);
        uintptr_t pdpt_virt_addr = pdpt_phys_addr;

        if (paging_has_been_set)
        {
            mem_create_physwindow(pdpt_phys_addr, &pdpt_virt_addr,
                                  PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
                                      PHYS_WINDOW_FLAG_LOCK);
        }
        pdpt = (page_t)pdpt_virt_addr;
    }
    else
    {
        if (paging_has_been_set)
        {
            mem_release_physwindow((uintptr_t)pml4);
        }
        return;
    }

    if (pdpt[index3] & 1)
    {
        uintptr_t pdp_phys_addr = pdpt[index3] & ~(0xFFF);
        uintptr_t pdp_virt_addr = pdp_phys_addr;
        if (paging_has_been_set)
        {
            mem_create_physwindow(pdp_phys_addr, &pdp_virt_addr,
                                  PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_WRITE |
                                      PHYS_WINDOW_FLAG_LOCK);
        }
        pdp = (page_t)pdp_virt_addr;
    }
    else
    {
        if (paging_has_been_set)
        {
            mem_release_physwindow((uintptr_t)pml4);
            mem_release_physwindow((uintptr_t)pdpt);
        }
        return;
    }

    if (pdp[index2] & 1)
    {
        uintptr_t pt_phys_addr = pdp[index2] & ~(0xFFF);
        uintptr_t pt_virt_addr = pt_phys_addr;
        if (paging_has_been_set)
        {
            mem_create_physwindow(pt_phys_addr, &pt_virt_addr, 0);
        }
        pt = (page_t)pt_virt_addr;
    }
    else
    {
        if (paging_has_been_set)
        {
            mem_release_physwindow((uintptr_t)pml4);
            mem_release_physwindow((uintptr_t)pdpt);
            mem_release_physwindow((uintptr_t)pdp);
        }
        return;
    }

    pt[index1] = 0;

    if (paging_has_been_set)
    {
        mem_release_physwindow((uintptr_t)pml4);
        mem_release_physwindow((uintptr_t)pdpt);
        mem_release_physwindow((uintptr_t)pdp);
        mem_release_physwindow((uintptr_t)pt);
    }

    // refresh cr3
    asm volatile("invlpg (%0)" ::"r"(virt) : "memory"); // flush the entry from TLB
}

void
paging_unmap_fill(page_t page_dir, uint64_t virt, size_t size)
{
    for (uint64_t i = 0; i < size; i++)
    {
        paging_unmap_page(page_dir, virt + i * 4096);
    }
}

void
paging_reload(page_t pml4)
{
    asm volatile("mov %0, %%cr3" ::"r"((uint64_t)pml4) : "memory");
    asm volatile("wbinvd" ::: "memory");
}

page_t
paging_get_highest_page_map(void)
{
    return pml4;
}

uint64_t
vaddr_to_paddr(page_t pml4, uint64_t vaddr)
{
    uint64_t index4 = (vaddr >> 39) & 0x1ff;
    uint64_t index3 = (vaddr >> 30) & 0x1ff;
    uint64_t index2 = (vaddr >> 21) & 0x1ff;
    uint64_t index1 = (vaddr >> 12) & 0x1ff;

    page_t pdpt = 0;
    page_t pdp  = 0;
    page_t pt   = 0;

    if (pml4[index4] & 1)
    {
        pdpt = (page_t)PHYS2VIRT((uint64_t)(pml4[index4] & ~(0xFFF)));
    }
    else
    {
        return 0;
    }

    if (pdpt[index3] & 1)
    {
        pdp = (page_t)PHYS2VIRT((uint64_t)(pdpt[index3] & ~(0xFFF)));
    }
    else
    {
        return 0;
    }

    if (pdp[index2] & 1)
    {
        pt = (page_t)PHYS2VIRT((uint64_t)(pdp[index2] & ~(0xFFF)));
    }
    else
    {
        return 0;
    }

    return pt[index1] & ~(0xFFF);
}
