#ifndef __LIBK_EXECUTABLE_ELF_H__
#define __LIBK_EXECUTABLE_ELF_H__

#include <libk/type.h>

typedef struct Elf64_Ehdr
{
    uint8_t  e_ident[16]; /* ELF identification */
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
    uint8_t  st_info;  /* Type and Binding attributes */
    uint8_t  st_other; /* Reserved */
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
    int64_t  r_addend; /* Addend */
} Elf64_Rela;

#define Elf64_R_SYM(i) ((i) >> 32)
#define Elf64_R_TYPE(i) ((i) & 0xffffffff)
#define ELF64_R_INFO(s, t) (((s) << 32) + ((t) & 0xffffffff))

enum Elf_Ptype : uint32_t
{
    PT_NULL    = 0, /* Unused entry */
    PT_LOAD    = 1, /* Loadable segment */
    PT_DYNAMIC = 2,
    PT_INTERP  = 3,
    PT_NOTE    = 4,
    PT_SHLIB   = 5,
    PT_PHDR    = 6,
    PT_LOPROC  = 0x70000000,
    PT_HIPROC  = 0x7fffffff
};

enum Elf_Dtag
{
    DT_NULL            = 0, /* Marks end of dynamic section */
    DT_NEEDED          = 1,
    DT_PLTRELSZ        = 2,
    DT_PLTGOT          = 3,
    DT_HASH            = 4,
    DT_STRTAB          = 5,
    DT_SYMTAB          = 6,
    DT_RELA            = 7,
    DT_RELASZ          = 8,
    DT_RELAENT         = 9,
    DT_STRSZ           = 10,
    DT_SYMENT          = 11,
    DT_INIT            = 12,
    DT_FINI            = 13,
    DT_SONAME          = 14,
    DT_RPATH           = 15,
    DT_SYMBOLIC        = 16,
    DT_REL             = 17,
    DT_RELSZ           = 18,
    DT_RELENT          = 19,
    DT_PLTREL          = 20,
    DT_DEBUG           = 21,
    DT_TEXTREL         = 22,
    DT_JMPREL          = 23,
    DT_BIND_NOW        = 24,
    DT_INIT_ARRAY      = 25,
    DT_FINI_ARRAY      = 26,
    DT_INIT_ARRAYSZ    = 27,
    DT_FINI_ARRAYSZ    = 28,
    DT_RUNPATH         = 29,
    DT_FLAGS           = 30,
    DT_ENCODING        = 32,
    DT_PREINIT_ARRAY   = 32,
    DT_PREINIT_ARRAYSZ = 33,
    DT_SYMTAB_SHNDX    = 34,
    DT_NUM             = 35,
    DT_LOOS            = 0x6000000d,
    DT_HIOS            = 0x6ffff000,
    DT_LOPROC          = 0x70000000,
    DT_HIPROC          = 0x7fffffff
};

enum Elf_Shtag
{
    SHT_NULL          = 0,
    SHT_PROGBITS      = 1,
    SHT_SYMTAB        = 2,
    SHT_STRTAB        = 3,
    SHT_RELA          = 4,
    SHT_HASH          = 5,
    SHT_DYNAMIC       = 6,
    SHT_NOTE          = 7,
    SHT_NOBITS        = 8,
    SHT_REL           = 9,
    SHT_SHLIB         = 10,
    SHT_DYNSYM        = 11,
    SHT_INIT_ARRAY    = 14,
    SHT_FINI_ARRAY    = 15,
    SHT_PREINIT_ARRAY = 16,
    SHT_GROUP         = 17,
    SHT_SYMTAB_SHNDX  = 18,
    SHT_RELR          = 19,

    // GNU / OS-specific range: 0x60000000 - 0x6fffffff
    SHT_GNU_ATTRIBUTES = 0x6ffffff5, // GNU object attributes
    SHT_GNU_HASH       = 0x6ffffff6, // GNU hash table untuk dynamic symbol
    SHT_GNU_LIBLIST    = 0x6ffffff7, // Daftar library GNU
    SHT_CHECKSUM       = 0x6ffffff8, // Checksum section
    SHT_LOSUNW         = 0x6ffffffa,
    SHT_SUNW_move      = 0x6ffffffa, // Sun-specific (relocation)
    SHT_SUNW_COMDAT    = 0x6ffffffb,
    SHT_SUNW_syminfo   = 0x6ffffffc,
    SHT_GNU_verdef     = 0x6ffffffd, // Version definition section
    SHT_GNU_verneed    = 0x6ffffffe, // Version dependency section
    SHT_GNU_versym     = 0x6fffffff, // Symbol version table

    // Processor-specific range: 0x70000000 - 0x7fffffff
    SHT_LOPROC = 0x70000000,
    SHT_HIPROC = 0x7fffffff,

    // Application-specific range: 0x80000000 - 0x8fffffff
    SHT_LOUSER = 0x80000000,
    SHT_HIUSER = 0x8fffffff
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
#define ET_EXEC 2
#define ET_NONE 0
enum Elf64_RelType
{
    R_X86_64_NONE      = 0,
    R_X86_64_64        = 1,
    R_X86_64_PC32      = 2,
    R_X86_64_GOT32     = 3,
    R_X86_64_PLT32     = 4,
    R_X86_64_COPY      = 5,
    R_X86_64_GLOB_DAT  = 6,
    R_X86_64_JUMP_SLOT = 7,
    R_X86_64_RELATIVE  = 8,
    R_X86_64_GOTPCREL  = 9,
    R_X86_64_32        = 10,
    R_X86_64_32S       = 11,
    R_X86_64_16        = 12,
    R_X86_64_PC16      = 13,
    R_X86_64_8         = 14,
    R_X86_64_PC8       = 15,

    // TLS Relocations
    R_X86_64_DTPMOD64 = 16, // Thread-Local Storage Module index
    R_X86_64_DTPOFF64 = 17, // TLS offset in module
    R_X86_64_TPOFF64  = 18, // TLS offset in thread
    R_X86_64_TLSGD    = 19, // TLS GD (global dynamic)
    R_X86_64_TLSLD    = 20, // TLS LD (local dynamic)
    R_X86_64_DTPOFF32 = 21, // TLS 32-bit
    R_X86_64_GOTTPOFF = 22, // TLS GOT offset
    R_X86_64_TPOFF32  = 23, // TLS 32-bit

    // More general
    R_X86_64_PC64            = 24,
    R_X86_64_GOTOFF64        = 25,
    R_X86_64_GOTPC32         = 26,
    R_X86_64_GOT64           = 27,
    R_X86_64_GOTPCREL64      = 28,
    R_X86_64_GOTPC64         = 29,
    R_X86_64_GOTPLT64        = 30,
    R_X86_64_PLTOFF64        = 31,
    R_X86_64_SIZE32          = 32,
    R_X86_64_SIZE64          = 33,
    R_X86_64_GOTPC32_TLSDESC = 34,
    R_X86_64_TLSDESC_CALL    = 35,
    R_X86_64_TLSDESC         = 36,

    // IFUNC
    R_X86_64_IRELATIVE = 37,

    R_X86_64_RELATIVE64 = 38
};

// dynamic
Elf64_Dyn *elf_get_phdr_dynamic(uint8_t *data);

typedef struct
{
    uint8_t    *strtab;
    uint64_t    needed;
    Elf64_Sym  *symbols;
    uint32_t    symcount;
    uint32_t    relasz;
    uint64_t    pltgot; // virt addr, need to be mapping
    Elf64_Rela *jmprel;
} elf_dynamic_map;

typedef struct
{
    Elf64_Shdr *symtab;
    Elf64_Shdr *strtab;
    Elf64_Shdr *gotplt;
    Elf64_Shdr *got;
} elf_section_map;

uintptr_t  elf_find_base_addr(uint8_t *data);
uint8_t   *elf_dyn_find(Elf64_Dyn *dyn, uint8_t *data, uint64_t tag);
void       elf_dyn_map_all(Elf64_Dyn *dyn, uint8_t *data, elf_dynamic_map *map);
void       elf_section_map_all(uint8_t *data, elf_section_map *map);
void       elf_mmap_got(elf_section_map *map);
Elf64_Sym *elf_dyn_find_symtab(uint8_t *data);

void      elf_load(uint8_t *data);
uintptr_t elf_get_entry(uint8_t *data);

#endif // __LIBK_EXECUTABLE_ELF_H__