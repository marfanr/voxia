#include "hal/cpu/paging.h"
#include "libk/serial.h"
#include "libk/type.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include <libk/executable/elf.h>
#include <libk/str.h>

#define ELF64_R_SYM(i) ((i) >> 32)          // ambil 32 bit atas → index simbol
#define ELF64_R_TYPE(i) ((i) & 0xffffffffL) // ambil 32 bit bawah → tipe
#define ELF64_R_INFO(s, t) (((Elf64_Xword)(s) << 32) + (Elf64_Xword)(t))

extern void module_loader(uintptr_t addr, uintptr_t stack);
boolean_t   elf_has_running    = false;
uintptr_t   rip_before_run_elf = 0;

uintptr_t
elf_find_base_addr(uint8_t *data)
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
    Elf64_Phdr *phdr = (Elf64_Phdr *)((uint64_t)data + ehdr->e_phoff);
    for (uint64_t i = 0; i < ehdr->e_shnum; i++)
    {
        Elf64_Phdr *p = (Elf64_Phdr *)((uint64_t)phdr + i * ehdr->e_phentsize);
        if (p->p_type == PT_LOAD)
        {
            return p->p_vaddr - p->p_offset;
        }
    }
}

Elf64_Dyn *
elf_get_phdr_dynamic(uint8_t *data)
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
    Elf64_Phdr *phdr = (Elf64_Phdr *)((uint64_t)data + ehdr->e_phoff);
    for (uint64_t i = 0; i < ehdr->e_shnum; i++)
    {
        Elf64_Phdr *p = (Elf64_Phdr *)((uint64_t)phdr + i * ehdr->e_phentsize);
        if (p->p_type == PT_DYNAMIC)
        {
            return (Elf64_Dyn *)((uint64_t)data + p->p_offset);
        }
    }
    return NULL;
}

void
elf_dyn_map_all(Elf64_Dyn *dyn, uint8_t *data, elf_dynamic_map *map)
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
    Elf64_Phdr *phdr = (Elf64_Phdr *)((uint64_t)data + ehdr->e_phoff);
    for (int i = 0; dyn[i].d_tag != DT_NULL; i++)
    {
        switch (dyn[i].d_tag)
        {
            case DT_STRTAB:
                map->strtab = (uint8_t *)((uint64_t)data + dyn[i].d_un.d_ptr);
                break;

            case DT_NEEDED:
                map->needed = dyn[i].d_un.d_val;
                break;

            case DT_SYMTAB:
                map->symbols = (Elf64_Sym *)((uint64_t)data + dyn[i].d_un.d_ptr);
                break;

            case DT_HASH:
                uint64_t vaddr  = dyn[i].d_un.d_ptr;
                uint64_t offset = 0;
                for (int j = 0; j < ehdr->e_phnum; j++)
                {
                    Elf64_Phdr *p = &phdr[j];
                    if (vaddr >= p->p_vaddr && vaddr < p->p_vaddr + p->p_memsz)
                    {
                        offset = (vaddr - p->p_vaddr) + p->p_offset;
                        break;
                    }
                }

                uint32_t *hash = (uint32_t *)(data + offset);
                map->symcount  = hash[1];
                break;

            case DT_PLTRELSZ:
                map->relasz = dyn[i].d_un.d_val;
                LOG_INFO("ELF", "pltrel size : %x", map->relasz);
                break;

            case DT_PLTGOT:
                LOG_INFO("ELF", "pltgot : 0x%x", dyn[i].d_un.d_ptr);
                map->pltgot = (uint64_t)dyn[i].d_un.d_ptr;
                break;

            case DT_JMPREL:
                map->jmprel = (Elf64_Rela *)(data + dyn[i].d_un.d_ptr);
                LOG_INFO("ELF", "jmprel : 0x%x", map->jmprel);
                break;

            case DT_RELA:
                LOG_INFO("ELF", "font rela at : 0x%x", dyn[i].d_un.d_ptr);
                break;
        }
    }
}

void
elf_section_map_all(uint8_t *data, elf_section_map *map)
{
    Elf64_Ehdr *ehdr      = (Elf64_Ehdr *)data;
    Elf64_Shdr *shdr      = (Elf64_Shdr *)((uint64_t)data + ehdr->e_shoff);
    Elf64_Shdr *sh_strtab = &shdr[ehdr->e_shstrndx]; // string table untuk nama section
    const char *sh_names  = (const char *)data + sh_strtab->sh_offset;
    for (uint64_t i = 0; i < ehdr->e_shnum; i++)
    {
        Elf64_Shdr *s        = (Elf64_Shdr *)&shdr[i];
        const char *sec_name = sh_names + s->sh_name;
        switch (s->sh_type)
        {
            case SHT_SYMTAB:
                map->symtab = s;
                break;

            case SHT_STRTAB:
                if (strncmp(sec_name, ".strtab", 7) == 0)
                {
                    map->strtab = s;
                }
                break;

            case SHT_PROGBITS:
                if (strncmp(sec_name, ".got.plt", 8) == 0)
                {
                    LOG_INFO("ELF", "found .got.plt at 0x%x, size %d", s->sh_addr, s->sh_size);
                    map->gotplt = s;
                }
                else if (strncmp(sec_name, ".got", 4) == 0)
                {
                    LOG_INFO("ELF", "found .plt at 0x%x, size %d", s->sh_addr, s->sh_size);
                    map->got = s;
                }
                break;

            case SHT_RELA:
                break;
        }
    }
}

void
elf_mmap_got(elf_section_map *map)
{
    if (map->gotplt)
    {
        uint64_t  start          = map->got ? map->got->sh_addr : map->gotplt->sh_addr;
        uint64_t  end            = map->gotplt->sh_addr + map->gotplt->sh_size;
        uint64_t  total          = end - start;
        uint64_t  alligned_start = ALIGN_DOWN(start, PAGE_SIZE);
        uintptr_t offset_aligned = start - alligned_start;
        uint64_t  total_aligned  = ALIGN_UP(total, PAGE_SIZE) / PAGE_SIZE;

        LOG_INFO("VOXMO", "gotplt found at 0x%x (0x%x), size %d", alligned_start, offset_aligned,
                 total_aligned);

        uintptr_t phys_addr = (uintptr_t)phys_base_alloc(total_aligned);
        paging_mmap_fill(paging_get_highest_page_map(), alligned_start, phys_addr, total_aligned,
                         0b111);
        paging_reload(paging_get_highest_page_map());
    }
}

uintptr_t
elf_get_entry(uint8_t *data)
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
    return ehdr->e_entry;
}

void
elf_load(uint8_t *data)
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
    LOG_INFO("ELF", "version : %d, ph num : %d", ehdr->e_version, ehdr->e_phnum);
    LOG_INFO("ELF", "entry : 0x%x", ehdr->e_entry);

    Elf64_Phdr *phdr = (Elf64_Phdr *)(data + ehdr->e_phoff);

    // find PT_LOAD
    for (uint64_t i = 0; i < ehdr->e_phnum; i++)
    {
        Elf64_Phdr *p             = (Elf64_Phdr *)((uint8_t *)phdr + i * ehdr->e_phentsize);
        uintptr_t   aligned_vaddr = ALIGN_DOWN(p->p_vaddr, BLOCK_SIZE);
        uintptr_t   vaddr_offset  = p->p_vaddr - aligned_vaddr;
        size_t      sz            = (vaddr_offset + p->p_memsz + BLOCK_SIZE - 1) / BLOCK_SIZE;
        LOG_INFO("ELF", "vaddr 0x%x, type %d", p->p_vaddr, p->p_type);

        if (p->p_type != PT_LOAD)
            continue;

        // LOG_INFO("ELF", "a : size %d", (int)sz);

        uintptr_t a = (uintptr_t)phys_base_alloc(sz);
        paging_mmap_fill(paging_get_highest_page_map(), aligned_vaddr, (uintptr_t)a, sz, 0b111);
        paging_reload(paging_get_highest_page_map());
        memcopy((void *)p->p_vaddr, (void *)((uint8_t *)data + p->p_offset), p->p_filesz);

        // call entry
    }

    LOG_INFO("ELF", "module loaded");
}