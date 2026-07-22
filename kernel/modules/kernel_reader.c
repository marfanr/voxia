#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "libk/executable/elf.h"
#include "libk/serial.h"
#include "libk/symbols.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include <modules/kernel_reader.h>
#include <str.h>
#include <vector.h>

symbols ksymbols;

INIT(KernelReader) {
	vector_init(&ksymbols.items);

	ksymbols.name = "kernel_symbols";

	size_t kernel_pages =
	    (ctx->kernel_raw_size + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
	uintptr_t addr = vma_lookup_free_vaddr(get_kernel_vmm_page(),
	                                       VMA_REGION_A, kernel_pages);
	paging_multiple_mmap(paging_get_highest_page_map(), addr,
	                     (uintptr_t)ctx->kernel_raw_addr, kernel_pages,
	                     PAGE_PRESENT | PAGE_WRITABLE);

	vma_register(get_kernel_vmm_page(), ctx->kernel_raw_addr, addr,
	             ctx->kernel_raw_size / BLOCK_SIZE,
	             PAGE_PRESENT | PAGE_WRITABLE | PAGE_NO_EXECUTE);

	Elf64_Ehdr* kernel_ehdr = (Elf64_Ehdr*)addr;
	LOG2_INFO("ELF", "kernel version : %d", kernel_ehdr->e_version);
	LOG2_INFO("ELF", "kernel entry : 0x%x", kernel_ehdr->e_entry);

	elf_section_map sh_ma = {0};
	elf_section_map_all((uint8_t*)addr, &sh_ma);

	Elf64_Sym* symtab = NULL;
	char* strtab = NULL;
	uint64_t sym_count = 0;

	if (sh_ma.symtab && sh_ma.strtab) {
		symtab = (Elf64_Sym*)((uint64_t)addr + sh_ma.symtab->sh_offset);
		strtab = (char*)((uint64_t)addr + sh_ma.strtab->sh_offset);
		sym_count = sh_ma.symtab->sh_size / sizeof(Elf64_Sym);
	} else if (sh_ma.dynsym && sh_ma.dynstr) {
		symtab = (Elf64_Sym*)((uint64_t)addr + sh_ma.dynsym->sh_offset);
		strtab = (char*)((uint64_t)addr + sh_ma.dynstr->sh_offset);
		sym_count = sh_ma.dynsym->sh_size / sizeof(Elf64_Sym);
	} else {
		/* Fallback into PT_DYNAMIC */
		Elf64_Dyn* dyn = elf_get_phdr_dynamic((uint8_t*)addr);
		if (dyn) {
			LOG2_INFO("ELF",
			         "using PT_DYNAMIC for searching simbol");
			elf_dynamic_map d_map = {0};
			elf_dyn_map_all(dyn, (uint8_t*)addr, &d_map);

			if (d_map.symbols && d_map.strtab) {
				symtab = d_map.symbols;
				strtab = (char*)d_map.strtab;
				sym_count = d_map.symcount;
			}
		}
	}

	if (!symtab || !strtab || sym_count == 0) {
		LOG_ERROR(
		    "ELF",
		    "symbol table not found (.symtab / .dynsym / PT_DYNAMIC)");
		return;
	}

	LOG2_INFO("ELF", "%d simbol kernel has beeen registered", sym_count);

	for (uint64_t i = 0; i < sym_count; i++) {
		const char* name = strtab + symtab[i].st_name;
		if (!*name || symtab[i].st_shndx == 0)
			continue;

		uintptr_t val = (uintptr_t)symtab[i].st_value;

		symbols_register(&ksymbols, name, val, symtab[i].st_size);
	}

	KDEBUG(DEBUG_LEVEL_INFO, "Kernel Symbol Reader Module Initialized\n");
}

symbols_ptr kernel_get_symbols() { return &ksymbols; }
