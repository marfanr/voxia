#include "paging.h"
#include "autoconf.h"
#include "init/init.h"
#include <libk/serial.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/phys_window.h>
#include <memory/vm_manager.h>
#include <spinlock.h>
#include <str.h>
#include <type.h>

static uintptr_t PHYS_BASE_METADATA_ADDR = 0xffffffe000000000;

#define EXPORT_SYMBOL(sym)                                                     \
	__attribute__((used, visibility("default"))) void* __export_##sym = &sym

volatile boolean_t paging_has_been_set = false;

extern uint8_t* bitmap_base_;
extern uint8_t* dma_bitmap_base_;
extern uint64_t metadata_size;
extern size_t dma_bitmap_size;

void paging_physwindow_mmap(page_t page_dir, uint64_t virt, uint64_t phys,
                            uint64_t flags);

typedef struct paging_page paging_page;
struct paging_page {
	uintptr_t page;
	paging_page* next;
} __attribute__((aligned(32)));

static spinlock_t paging_lock = {0};

page_t paging_create_page_directory() {
	spin_acquire(&paging_lock);
	page_t page = (page_t)phys_base_alloc(1);
	uintptr_t virtual_physwindow_addr = (uintptr_t)page;

	if (paging_has_been_set) {
		mem_create_physwindow((uintptr_t)page, &virtual_physwindow_addr,
		                      0);
	}

	memset((void*)virtual_physwindow_addr, 0, PAGE_SIZE);
	spin_release(&paging_lock);
	return page;
}

static page_t kernel_pml4;
static page_t physwindow_pt = 0;

void paging_physwindow_mmap(page_t page_dir, uint64_t virt, uint64_t phys,
                            uint64_t flags) {
	uint64_t index1 = (virt >> 12) & 0x1ff;
	if (!physwindow_pt) {
		uint64_t index4 = (virt >> 39) & 0x1ff;
		uint64_t index3 = (virt >> 30) & 0x1ff;
		uint64_t index2 = (virt >> 21) & 0x1ff;

		page_t p4 = page_dir;
		page_t pdpt = (page_t)VMM_PAGE;
		p4[index4] = (uint64_t)pdpt | flags;

		page_t pdp = (page_t)VMM_PAGE;
		pdpt[index3] = (uint64_t)pdp | flags;

		physwindow_pt = (page_t)VMM_PAGE;
		pdp[index2] = (uint64_t)physwindow_pt | flags;
	}

	physwindow_pt[index1] = (phys & PAGE_PHYS_MASK) | flags;

	asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
}

static void initialize_physical_paging_window(page_t page) {
	for (uint64_t i = 0; i < VOXIA_PHYS_MAX_WINDOW_COUNT; i++) {
		paging_physwindow_mmap(
		    page, (uint64_t)(mem_vma_phys_window_start + i * 0x1000), 0,
		    PAGE_PRESENT);
	}

	vxMultipleMmap(page, mem_vma_phys_window_pt, (uint64_t)physwindow_pt,
	               511, PAGE_PRESENT | PAGE_WRITABLE);

	LOG_INFO("PAGING", "mapping physwindow_pt 0x%lx to 0x%lx",
	         (uint64_t)physwindow_pt, (uint64_t)mem_vma_phys_window_pt);
	physwindow_pt = (page_t)mem_vma_phys_window_pt;
}

INIT(paging) {
	kernel_pml4 = VMM_PAGE;
	LOG_INFO("PAGING", "PML4: 0x%lx", (uint64_t)kernel_pml4);

	// paging_setup(pml4);
	for (uint64_t i = 0; i < 0x80000000; i += 0x1000) {
		vxMmap(kernel_pml4, (uint64_t)i + 0xffffffff80000000, i,
		       PAGE_PRESENT | PAGE_WRITABLE);
	}

	initialize_physical_paging_window(kernel_pml4);

	vxMultipleMmap(kernel_pml4, PHYS_BASE_METADATA_ADDR, (uint64_t)bitmap_base_,
	               metadata_size / PAGE_SIZE, PAGE_PRESENT | PAGE_WRITABLE);
	bitmap_base_ = (uint8_t*)PHYS_BASE_METADATA_ADDR;
	PHYS_BASE_METADATA_ADDR += metadata_size;

	paging_reload(kernel_pml4);

	LOG_INFO("PAGING", "paging setup done");

	paging_has_been_set = 1;
}

void paging_debug(page_t pml4_phys, uint64_t virt) {
	serial_trace("Debugging virtual address: 0x%lx\n", virt);

	uint64_t index4 = (virt >> 39) & 0x1ff;
	uint64_t index3 = (virt >> 30) & 0x1ff;
	uint64_t index2 = (virt >> 21) & 0x1ff;
	uint64_t index1 = (virt >> 12) & 0x1ff;

	uintptr_t pml4_virt = (uintptr_t)pml4_phys;
	mem_create_physwindow((uintptr_t)pml4_phys, &pml4_virt,
	                      PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_LOCK);
	page_t p4 = (page_t)pml4_virt;

	serial_trace("PML4[%lu] = 0x%lx flags 0b%b\n", index4, p4[index4],
	             p4[index4] & 0xFFF);

	if (!(p4[index4] & 1)) {
		serial_trace("PML4 entry not present\n");
		mem_release_physwindow((uintptr_t)p4);
		return;
	}

	uintptr_t pdpt_phys = p4[index4] & PAGE_PHYS_MASK;
	uintptr_t pdpt_virt = pdpt_phys;
	mem_create_physwindow(pdpt_phys, &pdpt_virt,
	                      PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_LOCK);
	page_t pdpt = (page_t)pdpt_virt;

	serial_trace("PDPT[%lu] = 0x%lx flags 0b%b\n", index3, pdpt[index3],
	             pdpt[index3] & 0xFFF);

	if (!(pdpt[index3] & 1)) {
		serial_trace("PDPT entry not present\n");
		mem_release_physwindow((uintptr_t)p4);
		mem_release_physwindow((uintptr_t)pdpt);
		return;
	}

	uintptr_t pdp_phys = pdpt[index3] & PAGE_PHYS_MASK;
	uintptr_t pdp_virt = pdp_phys;
	mem_create_physwindow(pdp_phys, &pdp_virt,
	                      PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_LOCK);
	page_t pdp = (page_t)pdp_virt;

	serial_trace("PDP[%lu] = 0x%lx flags 0b%b\n", index2, pdp[index2],
	             pdp[index2] & 0xFFF);

	if (!(pdp[index2] & 1)) {
		serial_trace("PDP entry not present\n");
		mem_release_physwindow((uintptr_t)p4);
		mem_release_physwindow((uintptr_t)pdpt);
		mem_release_physwindow((uintptr_t)pdp);
		return;
	}

	uintptr_t pt_phys = pdp[index2] & PAGE_PHYS_MASK;
	uintptr_t pt_virt = pt_phys;
	mem_create_physwindow(pt_phys, &pt_virt,
	                      PHYS_WINDOW_FLAG_READ | PHYS_WINDOW_FLAG_LOCK);
	page_t pt = (page_t)pt_virt;

	serial_trace("PT[%lu] = 0x%lx flags 0b%b\n", index1, pt[index1],
	             pt[index1] & 0xFFF);

	if (!(pt[index1] & 1))
		serial_trace("PT entry not present\n");
	else
		serial_trace("phys = 0x%lx\n", pt[index1] & PAGE_PHYS_MASK);

	mem_release_physwindow((uintptr_t)p4);
	mem_release_physwindow((uintptr_t)pdpt);
	mem_release_physwindow((uintptr_t)pdp);
	mem_release_physwindow((uintptr_t)pt);
}

// __attribute__((noreturn)) static void iddle() {
// 	for (;;)
// 		;
// }

void paging_setup(page_t p) {
	if (paging_has_been_set) {
		serial2_printf("called after pagging set\n");
		uintptr_t pml4_virt_addr = (uintptr_t)p;
		mem_create_physwindow((uintptr_t)p, &pml4_virt_addr,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);

		uintptr_t pml4_kernel_virt_addr = (uintptr_t)kernel_pml4;
		mem_create_physwindow((uintptr_t)kernel_pml4, &pml4_kernel_virt_addr,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);

		page_t dst = (page_t)pml4_virt_addr;
		page_t src = (page_t)pml4_kernel_virt_addr;

		for (int i = 256; i < 512; i++)
			dst[i] = src[i];

		mem_release_physwindow(pml4_virt_addr);
		mem_release_physwindow(pml4_kernel_virt_addr);
	}
}

void vxMmap(page_t page_dir, uint64_t virt, uint64_t phys, uint64_t flags) {
	spin_acquire(&paging_lock);

	uint64_t index4 = (virt >> 39) & 0x1ff;
	uint64_t index3 = (virt >> 30) & 0x1ff;
	uint64_t index2 = (virt >> 21) & 0x1ff;
	uint64_t index1 = (virt >> 12) & 0x1ff;

	uint64_t inter_flags =
	    (flags & ~PAGE_INTER_STRIP) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

	uint64_t phys_clean = phys & PAGE_PHYS_MASK;

	page_t p4 = page_dir;
	page_t pdpt = 0;
	page_t pdp = 0;
	page_t pt = 0;

	uintptr_t pml4_virt_addr = (uintptr_t)p4;
	if (paging_has_been_set) {
		mem_create_physwindow((uintptr_t)p4, &pml4_virt_addr,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);
	}
	p4 = (page_t)pml4_virt_addr;

	if (p4[index4] & 1) {
		uintptr_t pdpt_phys_addr = p4[index4] & PAGE_PHYS_MASK;
		uintptr_t pdpt_virt_addr = pdpt_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pdpt_phys_addr, &pdpt_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		pdpt = (page_t)pdpt_virt_addr;
		p4[index4] = pdpt_phys_addr | inter_flags;
	} else {
		uintptr_t pdpt_phys_addr = (uintptr_t)phys_base_alloc(1);
		uintptr_t pdpt_virt_addr = pdpt_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pdpt_phys_addr, &pdpt_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		memset((void*)pdpt_virt_addr, 0, PAGE_SIZE);
		pdpt = (page_t)pdpt_virt_addr;
		p4[index4] = pdpt_phys_addr | inter_flags;
	}

	if (pdpt[index3] & 1) {
		uintptr_t pdp_phys_addr = pdpt[index3] & PAGE_PHYS_MASK;
		uintptr_t pdp_virt_addr = pdp_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pdp_phys_addr, &pdp_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		pdp = (page_t)pdp_virt_addr;
		pdpt[index3] = pdp_phys_addr | inter_flags;
	} else {
		uintptr_t pdp_phys_addr = (uintptr_t)phys_base_alloc(1);
		uintptr_t pdp_virt_addr = pdp_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pdp_phys_addr, &pdp_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		memset((void*)pdp_virt_addr, 0, PAGE_SIZE);
		pdp = (page_t)pdp_virt_addr;
		pdpt[index3] = pdp_phys_addr | inter_flags;
	}

	if (pdp[index2] & 1) {
		uintptr_t pt_phys_addr = pdp[index2] & PAGE_PHYS_MASK;
		uintptr_t pt_virt_addr = pt_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pt_phys_addr, &pt_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		pt = (page_t)pt_virt_addr;
		pdp[index2] = pt_phys_addr | inter_flags;
	} else {
		uintptr_t pt_phys_addr = (uintptr_t)phys_base_alloc(1);
		uintptr_t pt_virt_addr = pt_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pt_phys_addr, &pt_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		memset((void*)pt_virt_addr, 0, PAGE_SIZE);
		pt = (page_t)pt_virt_addr;
		pdp[index2] = pt_phys_addr | inter_flags;
	}

	pt[index1] = phys_clean | flags;

	asm volatile("" ::: "memory");

	if (paging_has_been_set) {
		mem_release_physwindow((uintptr_t)p4);
		mem_release_physwindow((uintptr_t)pdpt);
		mem_release_physwindow((uintptr_t)pdp);
		mem_release_physwindow((uintptr_t)pt);
	}

	asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
	spin_release(&paging_lock);
}

void vxMultipleMmap(page_t page_dir, uint64_t virt, uint64_t phys,
                    uint64_t size, uint64_t flags) {
	uint64_t i = 0;

	for (; i + 3 < size; i += 4) {
		vxMmap(page_dir, virt + (i + 0) * 4096, phys + (i + 0) * 4096,
		       flags);
		vxMmap(page_dir, virt + (i + 1) * 4096, phys + (i + 1) * 4096,
		       flags);
		vxMmap(page_dir, virt + (i + 2) * 4096, phys + (i + 2) * 4096,
		       flags);
		vxMmap(page_dir, virt + (i + 3) * 4096, phys + (i + 3) * 4096,
		       flags);
	}

	for (; i < size; i++) {
		vxMmap(page_dir, virt + i * 4096, phys + i * 4096, flags);
	}
}

void paging_unmap_page(page_t page_dir, uint64_t virt) {
	spin_acquire(&paging_lock);

	uint64_t index4 = (virt >> 39) & 0x1ff;
	uint64_t index3 = (virt >> 30) & 0x1ff;
	uint64_t index2 = (virt >> 21) & 0x1ff;
	uint64_t index1 = (virt >> 12) & 0x1ff;

	page_t p4 = page_dir;
	page_t pdpt = 0;
	page_t pdp = 0;
	page_t pt = 0;

	uintptr_t pml4_virt_addr = (uintptr_t)p4;
	if (paging_has_been_set) {
		mem_create_physwindow((uintptr_t)p4, &pml4_virt_addr,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);
	}
	p4 = (page_t)pml4_virt_addr;

	if (p4[index4] & 1) {
		uintptr_t pdpt_phys_addr = p4[index4] & PAGE_PHYS_MASK;
		uintptr_t pdpt_virt_addr = pdpt_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pdpt_phys_addr, &pdpt_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		pdpt = (page_t)pdpt_virt_addr;
	} else {
		if (paging_has_been_set)
			mem_release_physwindow((uintptr_t)p4);
		spin_release(&paging_lock);
		return;
	}

	if (pdpt[index3] & 1) {
		uintptr_t pdp_phys_addr = pdpt[index3] & PAGE_PHYS_MASK;
		uintptr_t pdp_virt_addr = pdp_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pdp_phys_addr, &pdp_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		pdp = (page_t)pdp_virt_addr;
	} else {
		if (paging_has_been_set) {
			mem_release_physwindow((uintptr_t)p4);
			mem_release_physwindow((uintptr_t)pdpt);
		}
		spin_release(&paging_lock);
		return;
	}

	if (pdp[index2] & 1) {
		uintptr_t pt_phys_addr = pdp[index2] & PAGE_PHYS_MASK;
		uintptr_t pt_virt_addr = pt_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pt_phys_addr, &pt_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		pt = (page_t)pt_virt_addr;
	} else {
		if (paging_has_been_set) {
			mem_release_physwindow((uintptr_t)p4);
			mem_release_physwindow((uintptr_t)pdpt);
			mem_release_physwindow((uintptr_t)pdp);
		}
		spin_release(&paging_lock);
		return;
	}

	pt[index1] = 0;

	if (paging_has_been_set) {
		mem_release_physwindow((uintptr_t)p4);
		mem_release_physwindow((uintptr_t)pdpt);
		mem_release_physwindow((uintptr_t)pdp);
		mem_release_physwindow((uintptr_t)pt);
	}

	asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
	spin_release(&paging_lock);
}

void paging_unmap_fill(page_t page_dir, uint64_t virt, size_t size) {
	for (uint64_t i = 0; i < size; i++) {
		paging_unmap_page(page_dir, virt + i * 4096);
	}
}

void paging_reload(page_t p) {
	asm volatile("mfence\n\t"
	             "mov %0, %%cr3" ::"r"((uint64_t)p)
	             : "memory");
}

page_t paging_get_highest_page_map(void) { return kernel_pml4; }

uint64_t vaddr_to_paddr(page_t p, uint64_t vaddr) {
	uint64_t index4 = (vaddr >> 39) & 0x1ff;
	uint64_t index3 = (vaddr >> 30) & 0x1ff;
	uint64_t index2 = (vaddr >> 21) & 0x1ff;
	uint64_t index1 = (vaddr >> 12) & 0x1ff;

	page_t pdpt = 0;
	page_t pdp = 0;
	page_t pt = 0;

	if (p[index4] & 1)
		pdpt = (page_t)PHYS2VIRT(p[index4] & PAGE_PHYS_MASK);
	else
		return 0;

	if (pdpt[index3] & 1)
		pdp = (page_t)PHYS2VIRT(pdpt[index3] & PAGE_PHYS_MASK);
	else
		return 0;

	if (pdp[index2] & 1)
		pt = (page_t)PHYS2VIRT(pdp[index2] & PAGE_PHYS_MASK);
	else
		return 0;

	return pt[index1] & PAGE_PHYS_MASK;
}

void paging_make_cow(page_t page_dir, uint64_t virt) {
	spin_acquire(&paging_lock);

	uint64_t index4 = (virt >> 39) & 0x1ff;
	uint64_t index3 = (virt >> 30) & 0x1ff;
	uint64_t index2 = (virt >> 21) & 0x1ff;
	uint64_t index1 = (virt >> 12) & 0x1ff;

	page_t p4 = page_dir;
	page_t pdpt = 0;
	page_t pdp = 0;
	page_t pt = 0;

	uintptr_t pml4_virt_addr = (uintptr_t)p4;
	if (paging_has_been_set) {
		mem_create_physwindow((uintptr_t)p4, &pml4_virt_addr,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);
	}
	p4 = (page_t)pml4_virt_addr;

	if (p4[index4] & 1) {
		uintptr_t pdpt_phys_addr = p4[index4] & PAGE_PHYS_MASK;
		uintptr_t pdpt_virt_addr = pdpt_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pdpt_phys_addr, &pdpt_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		pdpt = (page_t)pdpt_virt_addr;
	} else {
		if (paging_has_been_set)
			mem_release_physwindow((uintptr_t)p4);
		spin_release(&paging_lock);
		return;
	}

	if (pdpt[index3] & 1) {
		uintptr_t pdp_phys_addr = pdpt[index3] & PAGE_PHYS_MASK;
		uintptr_t pdp_virt_addr = pdp_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pdp_phys_addr, &pdp_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		pdp = (page_t)pdp_virt_addr;
	} else {
		if (paging_has_been_set) {
			mem_release_physwindow((uintptr_t)p4);
			mem_release_physwindow((uintptr_t)pdpt);
		}
		spin_release(&paging_lock);
		return;
	}

	if (pdp[index2] & 1) {
		uintptr_t pt_phys_addr = pdp[index2] & PAGE_PHYS_MASK;
		uintptr_t pt_virt_addr = pt_phys_addr;
		if (paging_has_been_set) {
			mem_create_physwindow(pt_phys_addr, &pt_virt_addr,
			                      PHYS_WINDOW_FLAG_READ |
			                          PHYS_WINDOW_FLAG_WRITE |
			                          PHYS_WINDOW_FLAG_LOCK);
		}
		pt = (page_t)pt_virt_addr;
	} else {
		if (paging_has_been_set) {
			mem_release_physwindow((uintptr_t)p4);
			mem_release_physwindow((uintptr_t)pdpt);
			mem_release_physwindow((uintptr_t)pdp);
		}
		spin_release(&paging_lock);
		return;
	}

	if (pt[index1] & 1) {
		uint64_t entry = pt[index1];
		if (entry & PAGE_WRITABLE) {
			entry &= ~PAGE_WRITABLE;
			entry |= PAGE_COW;
			pt[index1] = entry;
		}
	}

	if (paging_has_been_set) {
		mem_release_physwindow((uintptr_t)p4);
		mem_release_physwindow((uintptr_t)pdpt);
		mem_release_physwindow((uintptr_t)pdp);
		mem_release_physwindow((uintptr_t)pt);
	}

	asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
	spin_release(&paging_lock);
}

uint64_t paging_get_entry(page_t page_dir, uint64_t virt) {
	spin_acquire(&paging_lock);

	uint64_t index4 = (virt >> 39) & 0x1ff;
	uint64_t index3 = (virt >> 30) & 0x1ff;
	uint64_t index2 = (virt >> 21) & 0x1ff;
	uint64_t index1 = (virt >> 12) & 0x1ff;

	page_t p4 = page_dir;
	page_t pdpt = 0;
	page_t pdp = 0;
	page_t pt = 0;

	uintptr_t pml4_virt_addr = (uintptr_t)p4;
	if (paging_has_been_set) {
		mem_create_physwindow((uintptr_t)p4, &pml4_virt_addr,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);
	}
	p4 = (page_t)pml4_virt_addr;

	if (!(p4[index4] & 1)) {
		if (paging_has_been_set) mem_release_physwindow((uintptr_t)p4);
		spin_release(&paging_lock);
		return 0;
	}

	uintptr_t pdpt_phys_addr = p4[index4] & PAGE_PHYS_MASK;
	uintptr_t pdpt_virt_addr = pdpt_phys_addr;
	if (paging_has_been_set) {
		mem_create_physwindow(pdpt_phys_addr, &pdpt_virt_addr,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);
	}
	pdpt = (page_t)pdpt_virt_addr;

	if (!(pdpt[index3] & 1)) {
		if (paging_has_been_set) {
			mem_release_physwindow((uintptr_t)p4);
			mem_release_physwindow((uintptr_t)pdpt);
		}
		spin_release(&paging_lock);
		return 0;
	}

	uintptr_t pdp_phys_addr = pdpt[index3] & PAGE_PHYS_MASK;
	uintptr_t pdp_virt_addr = pdp_phys_addr;
	if (paging_has_been_set) {
		mem_create_physwindow(pdp_phys_addr, &pdp_virt_addr,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);
	}
	pdp = (page_t)pdp_virt_addr;

	if (!(pdp[index2] & 1)) {
		if (paging_has_been_set) {
			mem_release_physwindow((uintptr_t)p4);
			mem_release_physwindow((uintptr_t)pdpt);
			mem_release_physwindow((uintptr_t)pdp);
		}
		spin_release(&paging_lock);
		return 0;
	}

	uintptr_t pt_phys_addr = pdp[index2] & PAGE_PHYS_MASK;
	uintptr_t pt_virt_addr = pt_phys_addr;
	if (paging_has_been_set) {
		mem_create_physwindow(pt_phys_addr, &pt_virt_addr,
		                      PHYS_WINDOW_FLAG_READ |
		                          PHYS_WINDOW_FLAG_WRITE |
		                          PHYS_WINDOW_FLAG_LOCK);
	}
	pt = (page_t)pt_virt_addr;

	uint64_t entry = 0;
	if (pt[index1] & 1) {
		entry = pt[index1];
	}

	if (paging_has_been_set) {
		mem_release_physwindow((uintptr_t)p4);
		mem_release_physwindow((uintptr_t)pdpt);
		mem_release_physwindow((uintptr_t)pdp);
		mem_release_physwindow((uintptr_t)pt);
	}

	spin_release(&paging_lock);
	return entry;
}