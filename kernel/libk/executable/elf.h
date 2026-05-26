#ifndef __LIBK_EXECUTABLE_ELF_H__
#define __LIBK_EXECUTABLE_ELF_H__

#include "libk/symbols.h"
#include <type.h>
#include <vector.h>

#define EI_MAG0		0
#define ELFMAG0		0x7f

#define EI_MAG1		1
#define ELFMAG1		'E'

#define EI_MAG2		2
#define ELFMAG2		'L'

#define EI_MAG3		3
#define ELFMAG3		'F'

#define	ELFMAG		"\177ELF"

typedef struct Elf64_Ehdr {
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

typedef struct Elf64_Phdr {
	uint32_t p_type;   /* Type of segment */
	uint32_t p_flags;  /* Segment attributes */
	uint64_t p_offset; /* Offset in file */
	uint64_t p_vaddr;  /* Virtual address in memory */
	uint64_t p_paddr;  /* Reserved */
	uint64_t p_filesz; /* Size of segment in file */
	uint64_t p_memsz;  /* Size of segment in memory */
	uint64_t p_align;  /* Alignment of segment */
} Elf64_Phdr;

typedef struct Elf64_Shdr {
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

typedef struct Elf64_Sym {
	uint32_t st_name;  /* Symbol name */
	uint8_t st_info;   /* Type and Binding attributes */
	uint8_t st_other;  /* Reserved */
	uint16_t st_shndx; /* Section table index */
	uint64_t st_value; /* Symbol value */
	uint64_t st_size;  /* Size of object (e.g., common) */
} Elf64_Sym;

typedef struct Elf64_Rel {
	uint64_t r_offset; /* Address */
	uint64_t r_info;   /* Relocation type and symbol index */
} Elf64_Rel;

typedef struct Elf64_Rela {
	uint64_t r_offset; /* Address */
	uint64_t r_info;   /* Relocation type and symbol index */
	int64_t r_addend;  /* Addend */
} Elf64_Rela;

enum Elf_Ptype : uint32_t {
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

enum Elf_Shdr_Flags {
	SHF_WRITE = 0x1,
	SHF_ALLOC = 0x2,
	SHF_EXECINSTR = 0x4,
	SHF_MERGE = 0x10,
	SHF_STRINGS = 0x20,
	SHF_INFO_LINK = 0x40,
	SHF_LINK_ORDER = 0x80,
	SHF_OS_NONCONFORMING = 0x100,
	SHF_GROUP = 0x200,
	SHF_TLS = 0x400,
	SHF_COMPRESSED = 0x800,
	SHF_MASKOS = 0x0FF00000,
	SHF_MASKPROC = 0xF0000000,
	SHF_ORDERED = 0x4000000,
	SHF_EXCLUDE = 0x8000000,
};

enum Elf_Dtag {
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

enum Elf_Shtag {
	SHT_NULL = 0,
	SHT_PROGBITS = 1,
	SHT_SYMTAB = 2,
	SHT_STRTAB = 3,
	SHT_RELA = 4,
	SHT_HASH = 5,
	SHT_DYNAMIC = 6,
	SHT_NOTE = 7,
	SHT_NOBITS = 8,
	SHT_REL = 9,
	SHT_SHLIB = 10,
	SHT_DYNSYM = 11,
	SHT_INIT_ARRAY = 14,
	SHT_FINI_ARRAY = 15,
	SHT_PREINIT_ARRAY = 16,
	SHT_GROUP = 17,
	SHT_SYMTAB_SHNDX = 18,
	SHT_RELR = 19,

	SHT_GNU_ATTRIBUTES = 0x6ffffff5,
	SHT_GNU_HASH = 0x6ffffff6,
	SHT_GNU_LIBLIST = 0x6ffffff7,
	SHT_CHECKSUM = 0x6ffffff8,
	SHT_LOSUNW = 0x6ffffffa,
	SHT_SUNW_move = 0x6ffffffa,
	SHT_SUNW_COMDAT = 0x6ffffffb,
	SHT_SUNW_syminfo = 0x6ffffffc,
	SHT_GNU_verdef = 0x6ffffffd,
	SHT_GNU_verneed = 0x6ffffffe,
	SHT_GNU_versym = 0x6fffffff,

	SHT_LOPROC = 0x70000000,
	SHT_HIPROC = 0x7fffffff,

	SHT_LOUSER = 0x80000000,
	SHT_HIUSER = 0x8fffffff
};

typedef struct Elf64_Dyn {
	uint64_t d_tag; /* Dynamic entry type */
	union {
		uint64_t d_val; /* Integer value */
		uint64_t d_ptr; /* Address value */
	} d_un;
} Elf64_Dyn;

enum Elf4_E_Type {
	ET_NONE = 0,
	ET_REL = 1,
	ET_EXEC = 2,
	ET_DYN = 3,
	ET_CORE = 4
};

enum Elf64_RelType {
	R_X86_64_NONE = 0,
	R_X86_64_64 = 1,
	R_X86_64_PC32 = 2,
	R_X86_64_GOT32 = 3,
	R_X86_64_PLT32 = 4,
	R_X86_64_COPY = 5,
	R_X86_64_GLOB_DAT = 6,
	R_X86_64_JUMP_SLOT = 7,
	R_X86_64_RELATIVE = 8,
	R_X86_64_GOTPCREL = 9,
	R_X86_64_32 = 10,
	R_X86_64_32S = 11,
	R_X86_64_16 = 12,
	R_X86_64_PC16 = 13,
	R_X86_64_8 = 14,
	R_X86_64_PC8 = 15,

	R_X86_64_DTPMOD64 = 16,
	R_X86_64_DTPOFF64 = 17,
	R_X86_64_TPOFF64 = 18,
	R_X86_64_TLSGD = 19,
	R_X86_64_TLSLD = 20,
	R_X86_64_DTPOFF32 = 21,
	R_X86_64_GOTTPOFF = 22,
	R_X86_64_TPOFF32 = 23,

	R_X86_64_PC64 = 24,
	R_X86_64_GOTOFF64 = 25,
	R_X86_64_GOTPC32 = 26,
	R_X86_64_GOT64 = 27,
	R_X86_64_GOTPCREL64 = 28,
	R_X86_64_GOTPC64 = 29,
	R_X86_64_GOTPLT64 = 30,
	R_X86_64_PLTOFF64 = 31,
	R_X86_64_SIZE32 = 32,
	R_X86_64_SIZE64 = 33,
	R_X86_64_GOTPC32_TLSDESC = 34,
	R_X86_64_TLSDESC_CALL = 35,
	R_X86_64_TLSDESC = 36,

	R_X86_64_IRELATIVE = 37,

	R_X86_64_RELATIVE64 = 38
};

Elf64_Dyn* elf_get_phdr_dynamic(uint8_t* data);
define_vector(uint64_t);

typedef struct {
	uint8_t* strtab;
	vector(uint64_t) needed;
	Elf64_Sym* symbols;
	uint64_t symcount;
	uint64_t pltgot;
	Elf64_Rela* jmprel;
	uint64_t pltrelsz;
	Elf64_Rela* rel;
	uint64_t relasz;
	uint64_t relaent;
} elf_dynamic_map;

typedef struct {
	Elf64_Shdr* symtab;
	Elf64_Shdr* strtab;
	Elf64_Shdr* gotplt;
	Elf64_Shdr* got;
	Elf64_Shdr* gnuhash;
	Elf64_Shdr* init_aray;
	Elf64_Shdr* fini_aray;
} elf_section_map;

uintptr_t elf_find_base_addr(uint8_t* data);
uint8_t* elf_dyn_find(Elf64_Dyn* dyn, uint8_t* data, uint64_t tag);
void elf_dyn_map_all(Elf64_Dyn* dyn, uint8_t* data, elf_dynamic_map* map);
void elf_section_map_all(uint8_t* data, elf_section_map* map);
void elf_mmap_got(volatile uintptr_t* page, elf_section_map* map,
                  uintptr_t base);

uintptr_t elf_get_entry(uint8_t* data, uintptr_t base);

typedef struct {
	uint32_t nbuckets;
	uint32_t symoffset;
	uint32_t bloom_size;
	uint32_t bloom_shift;

	uintptr_t* bloom;
	uint32_t* buckets;
	uint32_t* chains;
} GnuHashHeader;

struct elf_load_mmap_table {
	uintptr_t vaddr;
	uintptr_t alligned;
	uintptr_t paddr;
	size_t size;
	boolean_t mapped;
};

size_t elf_load(volatile uintptr_t* page, uint8_t* data,
                uintptr_t temporary_base, uintptr_t base,
                struct elf_load_mmap_table* table);

Elf64_Sym* elf_gnu_lookup(const char* name, GnuHashHeader* gh,
                          Elf64_Sym* symtab, const char* strtab);
void elf_gnu_hash_parse(GnuHashHeader* gnu_hash, Elf64_Shdr* gnu_hash_sym,
                        uint8_t* data);

void elf_relocate_dyn(elf_dynamic_map* map, uintptr_t base,
                      GnuHashHeader* gnu_hash,
                      symbols_ptr_vector_t* external_syms,
                      struct elf_load_mmap_table* table, int table_count);
					  
void elf_get_symbol(const char* sym_name, uintptr_t base, elf_section_map* map,
                    uint8_t* data, symbols_ptr_vector_t* syms,
                    boolean_t skip_empty_val);

typedef void (*ctor_t)(void);
void elf_call_init_array(elf_section_map* map, uintptr_t base);
uintptr_t elf_find_symbol(const char* name, GnuHashHeader* gnuhash,
                          uintptr_t base, elf_section_map* map, uint8_t* data);
uintptr_t elf_count_load_size(uint8_t* data);

#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xffffffffL)
#define ELF64_R_INFO(s, t) (((Elf64_Xword)(s) << 32) + (Elf64_Xword)(t))
#define ELF_PTR(type, base, off)                                               \
	((type*)ASSUME_ALIGNED(PTR_ADD((base), (off)), alignof(type)))

// debug
void elf_call_init_array2(elf_section_map* map, uintptr_t base);

#endif