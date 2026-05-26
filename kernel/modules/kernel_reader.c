#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/debug/debug.h"
#include "libk/executable/elf.h"
#include "libk/serial.h"
#include "libk/symbols.h"
#include <vector.h>
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include <str.h>
#include <modules/kernel_reader.h>

// define_vector(kernel_symbol);
// vector(kernel_symbol) kernel_symbols;
symbols ksymbols;

INIT(KernelReader) {
	// vector_init(&kernel_symbols);
	vector_init(&ksymbols.items);

	ksymbols.name = "kernel_symbols";

	uintptr_t addr = vma_lookup_free_vaddr(get_kernel_vmm_page(), 
		VMA_REGION_A, ctx->kernel_raw_size / BLOCK_SIZE);
	vxMultipleMmap(paging_get_highest_page_map(), addr,
		       (uintptr_t) ctx->kernel_raw_addr, ctx->kernel_raw_size,
		       0b111);
	paging_reload(paging_get_highest_page_map());
	vma_register(get_kernel_vmm_page(), ctx->kernel_raw_addr, addr,
		     ctx->kernel_raw_size / BLOCK_SIZE);

	Elf64_Ehdr* kernel_ehdr = (Elf64_Ehdr*) addr;
	LOG_INFO("ELF", "kernel version : %d", kernel_ehdr->e_version);
	LOG_INFO("ELF", "kernel entry : 0x%x", kernel_ehdr->e_entry);

	elf_section_map sh_ma = {0};
	elf_section_map_all((uint8_t*) addr, &sh_ma);
	// define_vector(

	if (!sh_ma.symtab) {
		LOG_ERROR("ELF", "tidak ada .symtab");
		return;
	}

	// // ambil section string table untuk simbol (biasanya .strtab)
	Elf64_Sym* symtab =
		(Elf64_Sym*) ((uint64_t) addr + sh_ma.symtab->sh_offset);
	char* strtab = (char*) ((uint64_t) addr + sh_ma.strtab->sh_offset);
	uint64_t sym_count = sh_ma.symtab->sh_size / sizeof(Elf64_Sym);

	for (uint64_t i = 0; i < sym_count; i++) {
		const char* name = strtab + symtab[i].st_name;
		if (!*name)
			continue;

		// LOG_INFO("ELF", "[%d] name=%s, value=0x%x size=%d", i, name,
		// 	 symtab[i].st_value, symtab[i].st_size);

		symbols_register(
			&ksymbols, (const char*) (strtab + symtab[i].st_name),
			(uintptr_t) symtab[i].st_value, symtab[i].st_size);
	}

	KDEBUG(DEBUG_LEVEL_INFO, "Kernel Symbol Reader Module Initialized\n");
}

symbols_ptr kernel_get_symbols() {
	return &ksymbols;
}
