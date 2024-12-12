#ifndef __LIBK_EXECUTABLE_ELF_H__
#define __LIBK_EXECUTABLE_ELF_H__

#include <libk/type.h>

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

#endif // __LIBK_EXECUTABLE_ELF_H__