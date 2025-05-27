#ifndef __LIBK_EXECUTABLE_ELF_H__
#define __LIBK_EXECUTABLE_ELF_H__

#include <libk/type.h>

typedef struct Elf64_Ehdr
{
    uint8_t e_ident[16];  /* ELF identification */
    uint16_t e_type;      /* Object file type */
    uint16_t e_machine;   /* Machine type */
    uint32_t e_version;   /* Object file version */
    uint64_t e_entry;     /* Entry point address */
    uint64_t e_phoff;     /* Program header offset */
    uint64_t e_shoff;     /* Section header offset */
    uint32_t e_flags;     /* Processor-specific flags */
    uint16_t e_ehsize;    /* ELF header size */
    uint16_t e_phentsize; /* Size of program header entry */
    uint16_t e_phnum;     /* Number of program header entries */
    uint16_t e_shentsize; /* Size of section header entry */
    uint16_t e_shnum;     /* Number of section header entries */
    uint16_t e_shstrndx;  /* Section name string table index */
} Elf64_Ehdr;

typedef struct Elf64_Phdr
{
    uint32_t p_type;   /* Type of segment */
    uint32_t p_flags;  /* Segment attributes */
    uint64_t p_offset; /* Offset in file */
    uint64_t p_vaddr;  /* Virtual address in memory */
    uint64_t p_paddr;  /* Reserved */
    uint64_t p_filesz; /* Size of segment in file */
    uint64_t p_memsz;  /* Size of segment in memory */
    uint64_t p_align;  /* Alignment of segment */
} Elf64_Phdr;

typedef struct Elf64_Shdr
{
    uint32_t sh_name;      /* Section name */
    uint32_t sh_type;      /* Section type */
    uint64_t sh_flags;     /* Section attributes */
    uint64_t sh_addr;      /* Virtual address in memory */
    uint64_t sh_offset;    /* Offset in file */
    uint64_t sh_size;      /* Size of section */
    uint32_t sh_link;      /* Link to other section */
    uint32_t sh_info;      /* Miscellaneous information */
    uint64_t sh_addralign; /* Address alignment boundary */
    uint64_t sh_entsize;   /* Size of entries, if section has table */
} Elf64_Shdr;

typedef struct Elf64_Sym
{
    uint32_t st_name;  /* Symbol name */
    uint8_t st_info;   /* Type and Binding attributes */
    uint8_t st_other;  /* Reserved */
    uint16_t st_shndx; /* Section table index */
    uint64_t st_value; /* Symbol value */
    uint64_t st_size;  /* Size of object (e.g., common) */
} Elf64_Sym;

typedef struct Elf64_Rel
{
    uint64_t r_offset; /* Address */
    uint64_t r_info;   /* Relocation type and symbol index */
} Elf64_Rel;

typedef struct Elf64_Rela
{
    uint64_t r_offset; /* Address */
    uint64_t r_info;   /* Relocation type and symbol index */
    int64_t r_addend;  /* Addend */
} Elf64_Rela;

#define Elf64_R_SYM(i) ((i) >> 32)
#define Elf64_R_TYPE(i) ((i)&0xffffffff)
#define ELF64_R_INFO(s, t) (((s) << 32) + ((t)&0xffffffff))

enum Elf_Ptype
{
    PT_NULL = 0, /* Unused entry */
    PT_LOAD = 1, /* Loadable segment */
    PT_DYNAMIC = 2,
    PT_INTERP = 3,
    PT_NOTE = 4,
    PT_SHLIB = 5,
    PT_PHDR = 6,
    PT_LOPROC = 0x70000000,
    PT_HIPROC = 0x7fffffff
};

enum Elf_Dtag
{
    DT_NULL = 0, /* Marks end of dynamic section */
    DT_NEEDED = 1,
    DT_PLTRELSZ = 2,
    DT_PLTGOT = 3,
    DT_HASH = 4,
    DT_STRTAB = 5,
    DT_SYMTAB = 6,
    DT_RELA = 7,
    DT_RELASZ = 8,
    DT_RELAENT = 9,
    DT_STRSZ = 10,
    DT_SYMENT = 11,
    DT_INIT = 12,
    DT_FINI = 13,
    DT_SONAME = 14,
    DT_RPATH = 15,
    DT_SYMBOLIC = 16,
    DT_REL = 17,
    DT_RELSZ = 18,
    DT_RELENT = 19,
    DT_PLTREL = 20,
    DT_DEBUG = 21,
    DT_TEXTREL = 22,
    DT_JMPREL = 23,
    DT_BIND_NOW = 24,
    DT_INIT_ARRAY = 25,
    DT_FINI_ARRAY = 26,
    DT_INIT_ARRAYSZ = 27,
    DT_FINI_ARRAYSZ = 28,
    DT_RUNPATH = 29,
    DT_FLAGS = 30,
    DT_ENCODING = 32,
    DT_PREINIT_ARRAY = 32,
    DT_PREINIT_ARRAYSZ = 33,
    DT_SYMTAB_SHNDX = 34,
    DT_NUM = 35,
    DT_LOOS = 0x6000000d,
    DT_HIOS = 0x6ffff000,
    DT_LOPROC = 0x70000000,
    DT_HIPROC = 0x7fffffff
};

typedef struct Elf64_Dyn
{
    uint64_t d_tag; /* Dynamic entry type */
    union
    {
        uint64_t d_val; /* Integer value */
        uint64_t d_ptr; /* Address value */
    } d_un;
} Elf64_Dyn;

#define ET_DYN 3

#endif // __LIBK_EXECUTABLE_ELF_H__