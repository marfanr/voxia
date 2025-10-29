#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/executable/elf.h"
#include "libk/serial.h"
#include "libk/vector.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include <libk/str.h>
#include <modules/kernel_reader.h>

define_vector(kernel_symbol);
vector(kernel_symbol) kernel_symbols;

INIT(kernel_reader)
{
    vector_init(&kernel_symbols);
    uintptr_t addr = vma_lookup_free_vaddr(VMA_REGION_C, ctx->kernel_raw_size / BLOCK_SIZE);
    paging_mmap_fill(paging_get_highest_page_map(), addr, (uintptr_t)ctx->kernel_raw_addr,
                     ctx->kernel_raw_size, 0b111);
    paging_reload(paging_get_highest_page_map());
    vma_register(ctx->kernel_raw_addr, addr, ctx->kernel_raw_size / BLOCK_SIZE);

    Elf64_Ehdr *kernel_ehdr = (Elf64_Ehdr *)addr;
    LOG_INFO("ELF", "kernel version : %d", kernel_ehdr->e_version);
    LOG_INFO("ELF", "kernel entry : 0x%x", kernel_ehdr->e_entry);

    Elf64_Shdr *shdr          = (Elf64_Shdr *)((uint64_t)addr + kernel_ehdr->e_shoff);
    Elf64_Shdr *sh_strtab_hdr = &shdr[kernel_ehdr->e_shstrndx];

    // uint8_t *lib_strtab = (uint8_t *)((uint64_t)addr + sh_strtab_hdr->sh_offset);

    elf_section_map sh_ma = {0};
    elf_section_map_all((uint8_t *)addr, &sh_ma);
    // define_vector(

    if (!sh_ma.symtab)
    {
        LOG_ERROR("ELF", "tidak ada .symtab");
        return;
    }

    // // ambil section string table untuk simbol (biasanya .strtab)
    Elf64_Sym *symtab    = (Elf64_Sym *)((uint64_t)addr + sh_ma.symtab->sh_offset);
    char      *strtab    = (char *)((uint64_t)addr + sh_ma.strtab->sh_offset);
    uint64_t   sym_count = sh_ma.symtab->sh_size / sizeof(Elf64_Sym);

    for (uint64_t i = 0; i < sym_count; i++)
    {
        const char *name = strtab + symtab[i].st_name;
        if (!*name)
            continue;

        LOG_INFO("ELF", "[%d] name=%s, value=0x%x size=%d", i, name, symtab[i].st_value,
                 symtab[i].st_size);

        kernel_symbol sym;
        sym.name  = name;
        sym.value = (uintptr_t)symtab[i].st_value;
        sym.size  = symtab[i].st_size;
        vector_push_back(&kernel_symbols, sym);
    }
}

kernel_symbol *
kernel_resolve_symbol(const char *name)
{
    kernel_symbol *sym;
    for (uint64_t i = 0; i < kernel_symbols.size; i++)
    {
        if (strncmp(kernel_symbols.data[i].name, name, strlen(name)) == 0)
        {
            sym = &kernel_symbols.data[i];
            return sym;
        }
    }
    return 0;
}