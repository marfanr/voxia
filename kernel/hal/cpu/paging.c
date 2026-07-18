#include "paging.h"
#include "autoconf.h"
#include "init/init.h"
#include <cpu/irq_lock.h>
#include <libk/serial.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/phys_window.h>
#include <memory/vm_manager.h>
#include <spinlock.h>
#include <str.h>
#include <type.h>

uint64_t g_hhdm_offset = 0;
volatile boolean_t paging_has_been_set = false;

extern uint8_t* bitmap_base_;
extern uint64_t metadata_size;
extern uint64_t higher_base_length_;
extern char _start[], __bss_end[], _data_start[], _rodata_start[],
    _rodata_end[];

static page_t kernel_pml4;
static page_t physwindow_pt_phys = 0;
static spinlock_t paging_lock = {0};

/* prototype */
void paging_physwindow_mmap(page_t page_dir_phys, uint64_t virt, uint64_t phys,
                            uint64_t flags);

static inline page_t get_table_vaddr(uintptr_t phys, uintptr_t* window_out) {
	if (!paging_has_been_set) {
		*window_out = 0;
		return (page_t)PHYS2VIRT(phys);
	}
	mem_create_physwindow(phys, window_out,
	                      PHYS_WINDOW_FLAG_WRITE | PHYS_WINDOW_FLAG_LOCK);
	return (page_t)*window_out;
}

static inline void release_table_vaddr(uintptr_t window) {
	if (paging_has_been_set && window)
		mem_release_physwindow(window);
}

static void flush_tlb_all() {
	uintptr_t cr3;
	asm volatile("mov %%cr3, %0" : "=r"(cr3));
	asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

page_t paging_create_page_directory() {
	uintptr_t flags = irq_save();
	spin_acquire(&paging_lock);
	uintptr_t phys = (uintptr_t)phys_base_alloc(1);
	uintptr_t win;
	page_t v = get_table_vaddr(phys, &win);
	memset((void*)v, 0, PAGE_SIZE_4KB);
	release_table_vaddr(win);
	spin_release(&paging_lock);
	irq_restore(flags);
	return (page_t)phys;
}

void paging_physwindow_mmap(page_t page_dir_phys, uint64_t virt, uint64_t phys,
                            uint64_t flags) {
	uint64_t hw_flags = flags & PAGE_HW_FLAG;

	if (!paging_has_been_set) {
		uint64_t idx4 = (virt >> 39) & PAGE_TABLE_INDEX_MASK,
		         idx3 = (virt >> 30) & PAGE_TABLE_INDEX_MASK,
		         idx2 = (virt >> 21) & PAGE_TABLE_INDEX_MASK,
		         idx1 = (virt >> 12) & PAGE_TABLE_INDEX_MASK;
		page_t p4 = (page_t)PHYS2VIRT(page_dir_phys);
		if (!(p4[idx4] & 1)) {
			uintptr_t pdpt = (uintptr_t)phys_base_alloc(1);
			memset((void*)PHYS2VIRT(pdpt), 0, PAGE_SIZE_4KB);
			p4[idx4] =
			    pdpt | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
		}
		page_t pdpt = (page_t)PHYS2VIRT(p4[idx4] & PAGE_PHYS_MASK);
		if (!(pdpt[idx3] & 1)) {
			uintptr_t pd = (uintptr_t)phys_base_alloc(1);
			memset((void*)PHYS2VIRT(pd), 0, PAGE_SIZE_4KB);
			pdpt[idx3] =
			    pd | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
		}
		page_t pd = (page_t)PHYS2VIRT(pdpt[idx3] & PAGE_PHYS_MASK);
		if (!(pd[idx2] & 1)) {
			if (!physwindow_pt_phys) {
				physwindow_pt_phys = (page_t)phys_base_alloc(1);
				memset((void*)PHYS2VIRT(physwindow_pt_phys), 0,
				       PAGE_SIZE_4KB);
			}
			pd[idx2] = (uint64_t)physwindow_pt_phys | PAGE_PRESENT |
			           PAGE_WRITABLE | PAGE_USER;
		}
		page_t pt = (page_t)PHYS2VIRT(pd[idx2] & PAGE_PHYS_MASK);
		pt[idx1] = (phys & PAGE_PHYS_MASK) | hw_flags;
		pt[511] = ((uintptr_t)physwindow_pt_phys & PAGE_PHYS_MASK) |
		          PAGE_PRESENT | PAGE_WRITABLE;
	} else {
		page_t pt =
		    (page_t)(mem_vma_phys_window_start + (511 * PAGE_SIZE_4KB));
		pt[(virt - mem_vma_phys_window_start) >> 12] =
		    (phys & PAGE_PHYS_MASK) | hw_flags;
		__asm__ volatile("mfence" ::: "memory");
	}
	INVLPG(virt)
}

static void _mmap_internal(page_t page_dir_phys, uint64_t virt, uint64_t phys,
                           uint64_t flags) {
	uint64_t idx[4] = {(virt >> 12) & PAGE_TABLE_INDEX_MASK,
	                   (virt >> 21) & PAGE_TABLE_INDEX_MASK,
	                   (virt >> 30) & PAGE_TABLE_INDEX_MASK,
	                   (virt >> 39) & PAGE_TABLE_INDEX_MASK};
	auto hw_flags = flags & PAGE_HW_FLAG;
	auto sw_table_flags =
	    PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);

	uintptr_t curr_phys = (uintptr_t)page_dir_phys, windows[4] = {0};
	page_t tables[4];

	for (int level = 3; level >= 0; level--) {
		tables[level] = get_table_vaddr(curr_phys, &windows[level]);
		uint64_t entry = tables[level][idx[level]];

		if (level == 2 && (flags & PAGE_1GB)) {
			tables[level][idx[level]] =
			    (phys & PAGE_PHYS_MASK) | hw_flags | PAGE_HUGE;
			goto release_table;
		}

		if (level == 1 && (flags & PAGE_2MB)) {
			tables[level][idx[level]] =
			    (phys & PAGE_PHYS_MASK) | hw_flags | PAGE_HUGE;
			goto release_table;
		}

		if (level == 0) {
			tables[level][idx[level]] =
			    (phys & PAGE_PHYS_MASK) | hw_flags;
			goto release_table;
		}

		if (!(entry & PAGE_PRESENT)) {

			uintptr_t next = (uintptr_t)phys_base_alloc(1);
			uintptr_t v_tmp;
			page_t p_tmp = get_table_vaddr(next, &v_tmp);
			memset((void*)p_tmp, 0, PAGE_SIZE_4KB);
			release_table_vaddr(v_tmp);

			tables[level][idx[level]] = next | sw_table_flags;
		} else if (entry & PAGE_HUGE) {

			uintptr_t old_phys = entry & PAGE_PHYS_MASK;
			uint64_t old_flags = entry & ~PAGE_PHYS_MASK;

			uintptr_t next = (uintptr_t)phys_base_alloc(1);
			uintptr_t v_tmp;
			page_t p_tmp = get_table_vaddr(next, &v_tmp);

			uint64_t step =
			    (level == 2) ? PAGE_SIZE_2MB : PAGE_SIZE_4KB;

			for (int i = 0; i < 512; i++) {
				p_tmp[i] = (old_phys + (uint64_t)i * step) |
				           (old_flags);

				if (level == 1)
					p_tmp[i] &= ~PAGE_HUGE;
			}

			release_table_vaddr(v_tmp);
			tables[level][idx[level]] = next | sw_table_flags;
		} else {
			tables[level][idx[level]] |= sw_table_flags;
		}

		curr_phys = tables[level][idx[level]] & PAGE_PHYS_MASK;
		__asm__ volatile("mfence" ::: "memory");
	}

release_table:
	for (int i = 0; i < 4; i++) {
		if (windows[i])
			release_table_vaddr(windows[i]);
	}
}

static void _mmap_range_internal(page_t page_dir, uint64_t virt, uint64_t phys,
                                 uint64_t size_in_pages, uint64_t flags) {
	uint64_t bytes_left = size_in_pages * PAGE_SIZE_4KB;
	uint64_t curr_virt = virt;
	uint64_t curr_phys = phys;

	while (bytes_left > 0) {

		if (bytes_left >= PAGE_SIZE_1GB &&
		    (curr_virt % PAGE_SIZE_1GB == 0) &&
		    (curr_phys == 0 || curr_phys % PAGE_SIZE_1GB == 0)) {
			_mmap_internal(page_dir, curr_virt, curr_phys,
			               flags | PAGE_1GB);
			curr_virt += PAGE_SIZE_1GB;
			if (curr_phys != 0)
				curr_phys += PAGE_SIZE_1GB;
			bytes_left -= PAGE_SIZE_1GB;
		}

		else if (bytes_left >= PAGE_SIZE_2MB &&
		         (curr_virt % PAGE_SIZE_2MB == 0) &&
		         (curr_phys == 0 || curr_phys % PAGE_SIZE_2MB == 0)) {
			_mmap_internal(page_dir, curr_virt, curr_phys,
			               flags | PAGE_2MB);
			curr_virt += PAGE_SIZE_2MB;
			if (curr_phys != 0)
				curr_phys += PAGE_SIZE_2MB;
			bytes_left -= PAGE_SIZE_2MB;
		}

		else {
			_mmap_internal(page_dir, curr_virt, curr_phys, flags);
			curr_virt += PAGE_SIZE_4KB;
			if (curr_phys != 0)
				curr_phys += PAGE_SIZE_4KB;
			bytes_left -= PAGE_SIZE_4KB;
		}
	}
}

void paging_mmap(page_t page_dir_phys, uint64_t virt, uint64_t phys,
                 uint64_t flags) {
	uintptr_t irq_flags = irq_save();
	spin_acquire(&paging_lock);
	_mmap_internal(page_dir_phys, virt, phys, flags);
	INVLPG(virt);
	spin_release(&paging_lock);
	irq_restore(irq_flags);
}

void paging_multiple_mmap(page_t page_dir, uint64_t virt, uint64_t phys,
                          uint64_t size_in_pages, uint64_t flags) {
	spin_acquire(&paging_lock);
	_mmap_range_internal(page_dir, virt, phys, size_in_pages, flags);

	if (size_in_pages >= 128) {
		flush_tlb_all();
	} else {
		for (uint64_t i = 0; i < size_in_pages; i++) {
			INVLPG(virt + (uint64_t)i * PAGE_SIZE_4KB);
		}
	}
	spin_release(&paging_lock);
}

INIT(paging) {
	g_hhdm_offset = ctx->hhdm_offset;
	kernel_pml4 = (page_t)phys_base_alloc(1);
	uintptr_t win;
	page_t v = get_table_vaddr((uintptr_t)kernel_pml4, &win);
	memset((void*)v, 0, PAGE_SIZE_4KB);
	release_table_vaddr(win);

	paging_physwindow_mmap(kernel_pml4, mem_vma_phys_window_start, 0, 0);

	/* Map kernel text (executable code) */
	for (uintptr_t v_ = ALIGN_DOWN(ctx->kernel_virt_addr, PAGE_SIZE_4KB);
	     v_ < ALIGN_UP((uintptr_t)_rodata_start, PAGE_SIZE_4KB);
	     v_ += PAGE_SIZE_4KB)
		paging_mmap(kernel_pml4, v_,
		            ctx->kernel_raw_addr + (v_ - ctx->kernel_virt_addr),
		            PAGE_PRESENT);

	/* Map Kernel BSS (uninitialized data) */
	for (uintptr_t v_ = ALIGN_DOWN((uintptr_t)_data_start, PAGE_SIZE_4KB);
	     v_ < ALIGN_UP((uintptr_t)__bss_end, PAGE_SIZE_4KB);
	     v_ += PAGE_SIZE_4KB)
		paging_mmap(kernel_pml4, v_,
		            ctx->kernel_raw_addr + (v_ - ctx->kernel_virt_addr),
		            PAGE_PRESENT | PAGE_WRITABLE | PAGE_NO_EXECUTE);

	/* Map Kernel Data (initialized data) */
	for (uintptr_t v_ = ALIGN_DOWN((uintptr_t)_rodata_start, PAGE_SIZE_4KB);
	     v_ < ALIGN_UP((uintptr_t)_rodata_end, PAGE_SIZE_4KB);
	     v_ += PAGE_SIZE_4KB)
		paging_mmap(kernel_pml4, v_,
		            ctx->kernel_raw_addr + (v_ - ctx->kernel_virt_addr),
		            PAGE_PRESENT | PAGE_NO_EXECUTE);

	/* Map Kernel Stack */
	uintptr_t rsp;
	__asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
	uintptr_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	uintptr_t stack_phys =
	    vaddr_to_paddr((page_t)(cr3 & PAGE_PHYS_MASK), rsp);
	if (!stack_phys)
		stack_phys =
		    (rsp >= 0xffffffff80000000ULL)
		        ? rsp - (ctx->kernel_virt_addr - ctx->kernel_raw_addr)
		        : rsp - ctx->hhdm_offset;

	paging_multiple_mmap(kernel_pml4, ALIGN_DOWN(rsp - 0x10000, 0x1000),
	                     ALIGN_DOWN(stack_phys - 0x10000, 0x1000), 0x11,
	                     PAGE_PRESENT | PAGE_WRITABLE);

	/* Update Phys Base Allocator Bitmap */
	paging_multiple_mmap(kernel_pml4, KALLOC_BASE_ADDR,
	                     (uintptr_t)bitmap_base_ - ctx->hhdm_offset,
	                     ALIGN_UP(metadata_size, PAGE_SIZE_4KB) / 0x1000,
	                     PAGE_PRESENT | PAGE_WRITABLE);

	asm volatile("cli");
	paging_reload(kernel_pml4);
	paging_has_been_set = true;
	bitmap_base_ = (uint8_t*)KALLOC_BASE_ADDR;
	asm volatile("sti");

	serial2_printf("bitmap base now 0x%lx (%d)\n", (uintptr_t)bitmap_base_,
	               metadata_size / 1024);
}

void paging_reload(page_t p) {
	asm volatile("mfence\n\tmov %0, %%cr3" ::"r"((uintptr_t)p) : "memory");
}

page_t paging_get_highest_page_map(void) {
	if (paging_has_been_set) {
		uint64_t cr3;
		asm volatile("mov %%cr3, %0" : "=r"(cr3));
		return (page_t)(cr3 & PAGE_PHYS_MASK);
	}
	return kernel_pml4;
}

void paging_setup(page_t p_phys) {
	uintptr_t v_dst, v_src;
	page_t dst = get_table_vaddr((uintptr_t)p_phys, &v_dst);
	page_t src = get_table_vaddr((uintptr_t)kernel_pml4, &v_src);
	for (int i = 256; i < 512; i++)
		dst[i] = src[i];
	release_table_vaddr(v_dst);
	release_table_vaddr(v_src);
}

uint64_t paging_get_entry_ext(page_t p_phys, uint64_t vaddr, int* level_out) {
	uint64_t idx[4] = {(vaddr >> 12) & PAGE_TABLE_INDEX_MASK,
	                   (vaddr >> 21) & PAGE_TABLE_INDEX_MASK,
	                   (vaddr >> 30) & PAGE_TABLE_INDEX_MASK,
	                   (vaddr >> 39) & PAGE_TABLE_INDEX_MASK};
	uintptr_t curr = (uintptr_t)p_phys, win;
	for (int i = 3; i >= 0; i--) {
		page_t table = get_table_vaddr(curr, &win);
		uint64_t entry = table[idx[i]];
		release_table_vaddr(win);
		if (!(entry & 1))
			return 0;
		if (level_out)
			*level_out = i;
		if ((i == 2 || i == 1) && (entry & PAGE_HUGE))
			return entry;
		if (i == 0)
			return entry;
		curr = entry & PAGE_PHYS_MASK;
	}
	return 0;
}

uint64_t paging_get_entry(page_t p_phys, uint64_t vaddr) {
	return paging_get_entry_ext(p_phys, vaddr, 0);
}

uint64_t vaddr_to_paddr(page_t p_phys, uint64_t vaddr) {
	int level = 0;
	uint64_t entry = paging_get_entry_ext(p_phys, vaddr, &level);
	if (!entry)
		return 0;
	if (entry & PAGE_HUGE) {
		uint64_t mask = (level == 2) ? 0x3FFFFFFFULL : 0x1FFFFFULL;
		return (entry & PAGE_PHYS_MASK & ~mask) | (vaddr & mask);
	}
	return (entry & PAGE_PHYS_MASK) | (vaddr & 0xFFF);
}

void paging_make_cow(page_t page_dir, uint64_t virt) {
	int level = 0;
	uint64_t entry = paging_get_entry_ext(page_dir, virt, &level);
	if (!(entry & PAGE_WRITABLE))
		return;

	uint64_t new_flags = (entry & ~PAGE_WRITABLE) | PAGE_COW;

	if (entry & PAGE_HUGE) {
		if (level == 2)
			new_flags |= PAGE_1GB;
		else
			new_flags |= PAGE_2MB;
	}

	paging_mmap(page_dir, virt, entry & PAGE_PHYS_MASK, new_flags);
}

void paging_unmap_page(page_t page_dir, uint64_t virt) {
	paging_mmap(page_dir, virt, 0, 0);
}

void paging_sync_kernel_entry(uint64_t vaddr) {
	if (vaddr < 0xFFFF800000000000ULL)
		return;

	uint64_t idx4 = (vaddr >> 39) & 0x1FF;
	page_t current_pml4_phys = paging_get_highest_page_map();
	if ((uintptr_t)current_pml4_phys == (uintptr_t)kernel_pml4)
		return;

	uintptr_t flags = irq_save();
	spin_acquire(&paging_lock);
	uintptr_t k_win, c_win;
	page_t k_pml4 = get_table_vaddr((uintptr_t)kernel_pml4, &k_win);
	page_t c_pml4 = get_table_vaddr((uintptr_t)current_pml4_phys, &c_win);

	if ((k_pml4[idx4] & PAGE_PRESENT) && !(c_pml4[idx4] & PAGE_PRESENT)) {
		c_pml4[idx4] = k_pml4[idx4];
	}

	release_table_vaddr(k_win);
	release_table_vaddr(c_win);
	spin_release(&paging_lock);
	irq_restore(flags);
}

void paging_multiple_unmap(page_t page_dir, uint64_t virt,
                           size_t size_in_pages) {
	spin_acquire(&paging_lock);
	_mmap_range_internal(page_dir, virt, 0, size_in_pages, 0);

	if (size_in_pages >= 128) {
		flush_tlb_all();
	} else {
		for (uint64_t i = 0; i < size_in_pages; i++) {
			INVLPG(virt + (uint64_t)i * PAGE_SIZE_4KB);
		}
	}
	spin_release(&paging_lock);
}
