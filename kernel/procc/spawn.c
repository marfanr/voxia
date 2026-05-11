#include "spawn.h"
#include "task.h"
#include <hal/cpu/paging.h>
#include <libk/executable/elf.h>
#include <libk/serial.h>
#include <str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <sys/library.h>
#include <vfs/vfs.h>

extern boolean_t is_running_program;
extern void __memcopy(void* dest, void* src, uint32_t size);

uint64_t setup_user_stack(uint64_t argc, uint64_t vaddr, uint64_t* rsp,
			  char** argv, char** envp) {
	uint64_t stack_top_vadr = vaddr;

	// Push NULL terminator for envp
	*--rsp = 0;
	stack_top_vadr -= sizeof(uint64_t);
	for (int i = 0; envp[i]; i++) {
		*--rsp = (uint64_t) envp[i];
		stack_top_vadr -= sizeof(uint64_t);
	}

	// Push NULL terminator for argv
	*--rsp = 0;
	stack_top_vadr -= sizeof(uint64_t);
	for (int i = argc - 1; i >= 0; i--) {
		*--rsp = (uint64_t) argv[i];
		stack_top_vadr -= sizeof(uint64_t);
	}

	// Push argc
	*--rsp = argc;
	stack_top_vadr -= sizeof(uint64_t);

	*(--rsp) = (uint64_t) 0x0240000000;
	stack_top_vadr -= sizeof(uint64_t);
	return stack_top_vadr;
}

void setup_stack(uintptr_t stack_addr) {
	serial_trace("stack addr : 0x%x\n", stack_addr);
	// setup stack
	uint64_t* rsp = (uint64_t*) stack_addr;

	// set rsp
	asm("mov %0, %%rsp" ::"r"(rsp + 4096));
	asm("and $-16, %rsp");
}

#define PAGE_PRESENT 0x1
#define PAGE_WRITE (0b1 << 2)
#define PAGE_EXECUTE (0b1 << 63)
#define PAGE_USER (0b1 << 3)

#define PROT_READ 4
#define PROT_WRITE 0x2
#define PROT_EXEC 1

uint64_t pflags_to_page_flags(uint32_t pflags) {
	uint64_t flags = 0;
	if (pflags & PROT_READ)
		flags |= PAGE_PRESENT;
	if (pflags & PROT_WRITE)
		flags |= PAGE_PRESENT | PAGE_WRITE;
	if (pflags & PROT_EXEC)
		flags |= PAGE_PRESENT | PAGE_EXECUTE;
	return flags | PAGE_USER;
}

// extern void __r();
extern void jump_usermode(uint64_t entry, uint64_t stack);

int spawn(const char* path, char* argv[], char* envp[]) {
	serial_trace("\nspawn %s\n", path);
	// auto                  fd = vxFileInternalOpen(path, 0);
	// struct vfs_file_stats stats;
	// int                   fs_resp = vxVFSFileStat(fd, &stats);
	// serial_trace("file size : %d\n", stats.size);

	// uint8_t *file = (uint8_t *)(vxPhysBaseAlloc(2 + stats.size / 4096));
	// int      read = vxVFSRead(fd, file, stats.size);
	// if (read != 0)
	//     serial_trace("error reading file\n");
	uint8_t* file = 0;

	Elf64_Ehdr* ehdr = (Elf64_Ehdr*) file;
	serial_trace("version : %d", ehdr->e_version);
	serial_trace("entry : 0x%x", ehdr->e_entry);

	Elf64_Phdr* phdr = (Elf64_Phdr*) ((uint64_t) ehdr + ehdr->e_phoff);
	Elf64_Shdr* shdr = (Elf64_Shdr*) ((uint64_t) ehdr + ehdr->e_shoff);

	page_t page_ = (page_t) PHYS2VIRT(VMM_PAGE);
	serial_trace("page_ : 0x%x\n", page_);
	paging_setup(page_);

	uint8_t* str_tab =
		(uint8_t*) ((uint64_t) ehdr + shdr[ehdr->e_shstrndx].sh_offset);
	Elf64_Shdr* symtab = NULL;
	for (int i = 0; i < ehdr->e_shnum; i++) {
		if (strncmp((char*) (str_tab + shdr[i].sh_name), ".symtab", 7)
		    == 0) {
			serial_trace("symtab found at %d\n", i);
			symtab = &shdr[i];
			break;
		}
	}

	Elf64_Sym* symbols = (Elf64_Sym*) ((uint64_t) file + symtab->sh_offset);
	// for (int i = 0; i < symtab->sh_size / sizeof (Elf64_Sym); i++)
	//     {
	//         serial_trace (
	//             "Function %s: Start: 0x%x, Size: %d bytes, end : 0x%x\n",
	//             str_tab + symbols[i].st_name, symbols[i].st_value,
	//             symbols[i].st_size, symbols[i].st_value +
	//             symbols[i].st_size);
	//     }

	for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
		phdr = (Elf64_Phdr*) ((uint64_t) phdr + ehdr->e_phentsize);
	}

	serial_trace("\n");

	for (uint64_t i = 0; i < ehdr->e_shnum; i++) {
		char* name = (char*) (str_tab + shdr->sh_name);
		if (strncmp(name, ".dynamic", 5) == 0) {
			serial_trace("dynamic section found\n");

			break;
		}
		serial_trace("shdr name : %s , ", name);
		serial_trace("shdr addr : 0x%x\n", shdr->sh_addr);
		shdr = (Elf64_Shdr*) ((uint64_t) shdr + ehdr->e_shentsize);
	}

	// program entry offset
	uint64_t entry = ehdr->e_entry;
	serial_trace("entry : 0x%x\n", entry);

	// load program to memory
	Elf64_Phdr* phdr2 = (Elf64_Phdr*) ((uint64_t) ehdr + ehdr->e_phoff);
	Elf64_Dyn* dyn = 0;

	int loop = 0;
	uint64_t base = 0;
	for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
		if (phdr2->p_type == PT_DYNAMIC) {
			serial_trace("dynamic pheader found\n");
			dyn = (Elf64_Dyn*) ((uint8_t*) (file
							+ phdr2->p_offset));
		} else if (phdr2->p_type == PT_LOAD) {
			void* a = (void*) (vxPhysBaseAlloc(
				1 + phdr2->p_memsz / 4096));
			uintptr_t allignment_vadr = phdr2->p_vaddr & ~0xFFF;
			uintptr_t offset_page =
				phdr2->p_vaddr - allignment_vadr;

			// if ((uint64_t)a % 0x1000 != 0) {
			//     serial_trace("ERROR: PADDRNOT ALIGNMENT AT
			//     4KB\n");
			// }
			// if (phdr2->p_vaddr % 0x1000 != 0) {
			//     serial_trace("ERROR: VADDR NOT ALIGNMENT AT
			//     4KB\n"); serial_trace("alignment vaddr : 0x%x\n",
			//     allignment_vadr);
			// }
			uint64_t round_up_size =
				(phdr->p_memsz + 0xFFF) & ~0xFFF;

			serial_trace("mapping 0x%x to 0x%x size %d until 0x%x "
				     "(real end 0x%x)\n",
				     allignment_vadr, a, phdr2->p_memsz,
				     allignment_vadr + phdr2->p_memsz,
				     allignment_vadr + 4096);

			if (base == 0) {
				base = phdr2->p_vaddr - phdr2->p_offset;
			}

			// chec apakah a aligned 4kb
			// serial_trace("a : 0x%x\n", a);

			for (uint64_t j = 0; j < phdr2->p_memsz; j++) {
				*(uint8_t*) ((uint64_t) a + offset_page + j) =
					*(uint8_t*) (file + phdr2->p_offset
						     + j);
			}
			vxMultipleMmap(page_, phdr2->p_vaddr,
				       VIRT2PHYS((uintptr_t) a),
				       round_up_size / 4096, 0b111);

			paging_reload((page_t) VIRT2PHYS((uint64_t) page_));
			serial_trace("mapped addr : 0x%x\n",
				     vaddr_to_paddr((page_t) ((uint64_t) page_),
						    phdr2->p_vaddr));
		}
		// phdr2 = (Elf64_Phdr *)((uint64_t)phdr2 + ehdr->e_phentsize);
	}
	serial_trace("base : 0x%x\n", base);
	serial_trace("entry : 0x%x\n", entry);

	// dnynamic linking part
	uint8_t* init_func_addr = 0;
	if (dyn != 0) {
		uint64_t plt_relz = 0;
		Elf64_Rela* plt_rela = 0;
		uint8_t* strtab = NULL;
		uint64_t dyn_needed = 0;
		Elf64_Sym* symbols = 0;

		for (int i = 0; dyn[i].d_tag != DT_NULL; i++) {
			switch (dyn[i].d_tag) {
			case DT_STRTAB:
				strtab =
					(uint8_t*) ((uint64_t) file
						    + dyn[i].d_un.d_ptr - base);
				serial_trace("strtab : 0x%x\n", strtab);
				break;

			case DT_NEEDED:
				dyn_needed = dyn[i].d_un.d_val;
				break;

			case DT_PLTRELSZ:
				plt_relz =
					dyn[i].d_un.d_val / sizeof(Elf64_Rela);
				serial_trace("relsz : %d\n", plt_relz);
				break;

			case DT_PLTGOT:
				serial_trace("pltgot : 0x%x\n",
					     dyn[i].d_un.d_ptr);
				void* c = (void*) (vxPhysBaseAlloc(
					1
					+ plt_relz * sizeof(Elf64_Rela)
						  / 4096));
				uintptr_t allignment_vadr =
					dyn[i].d_un.d_ptr & ~0xFFF;

				if ((uint64_t) c % 0x1000 != 0) {
					serial_trace("ERROR: PADDRNOT "
						     "ALIGNMENT AT 4KB\n");
				}
				if (dyn[i].d_un.d_ptr % 0x1000 != 0) {
					serial_trace("ERROR: VADDR NOT "
						     "ALIGNMENT AT 4KB\n");
				}
				uint64_t round_up_size =
					(phdr->p_memsz + 0xFFF) & ~0xFFF;

				vxMmap(page_, allignment_vadr,
				       VIRT2PHYS((uint64_t) c), 0b111);
				// paging_reload(VIRT2PHYS(page_));
				break;

			case DT_JMPREL:
				serial_trace("jmprel : 0x%x\n",
					     dyn[i].d_un.d_ptr);
				plt_rela = (Elf64_Rela*) (file
							  + (dyn[i].d_un.d_ptr
							     - base));
				break;

			case DT_SYMTAB:
				serial_trace("symtab : 0x%x\n",
					     dyn[i].d_un.d_ptr);
				symbols = (Elf64_Sym*) ((uint64_t) file
							+ (dyn[i].d_un.d_ptr
							   - base));
				break;

			default:
				break;
			}
		}

		Elf64_Sym* lib_dynsym = 0;
		uint8_t* dynstr = 0;
		uint64_t lib_dynsym_size = 0;

		// read shared library part
		struct Library* lib = 0;
		if (strtab && dyn_needed) {
			serial_trace("strtab 0x%x neded %d\n", strtab,
				     dyn_needed);
			const char* library_name =
				(const char*) (strtab + dyn_needed);
			serial_trace("needed : %s\n", library_name);
			serial_trace("\n");

			lib = library_load(library_name);
			if (lib == 0) {
				serial_trace("error loading library\n");
				return -1;
			}
			Elf64_Ehdr* ehdr = (Elf64_Ehdr*) lib->entry;
			// cek apakah ini shared library
			if (ehdr->e_type == ET_DYN) {
				serial_trace("ini adalah shared library\n");
			}
			Elf64_Phdr* phdr =
				(Elf64_Phdr*) ((uint64_t) ehdr + ehdr->e_phoff);
			Elf64_Shdr* shdr =
				(Elf64_Shdr*) ((uint64_t) ehdr + ehdr->e_shoff);

			uint8_t* lib_strtab =
				(uint8_t*) ((uint64_t) ehdr
					    + shdr[ehdr->e_shstrndx].sh_offset);

			// parse secton header pada library
			for (uint64_t i = 0; i < ehdr->e_shnum; i++) {
				char* name =
					(char*) (lib_strtab + shdr->sh_name);
				if (strncmp(name, ".dynsym", 7) == 0) {
					serial_trace("dynamic symbol found\n");
					serial_trace("size : %d\n",
						     shdr->sh_size);
					lib_dynsym =
						(Elf64_Sym*) (lib->entry
							      + shdr->sh_offset);
					lib_dynsym_size = shdr->sh_size;
				} else if (strncmp(name, ".dynstr", 7) == 0) {
					serial_trace("dynamic string found\n");
					dynstr = (uint8_t*) (lib->entry
							     + shdr->sh_offset);
				}

				// serial_trace ("shdr name : %s , ", name);
				// serial_trace ("shdr addr : 0x%x\n",
				// shdr->sh_addr);
				shdr = (Elf64_Shdr*) ((uint64_t) shdr
						      + ehdr->e_shentsize);
			}

			// read .dynsym
			if (lib_dynsym != 0) {
				for (int i = 0;
				     i < lib_dynsym_size / sizeof(Elf64_Sym);
				     i++) {
					serial_trace(
						"Function %s: Start: 0x%x, "
						"Size: %d bytes, end : 0x%x\n",
						dynstr + lib_dynsym[i].st_name,
						lib_dynsym[i].st_value,
						lib_dynsym[i].st_size,
						lib_dynsym[i].st_value
							+ lib_dynsym[i]
								  .st_size);
				}
			}

			// read progra header
			Elf64_Dyn* libdyn = 0;
			for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
				if (phdr->p_type == PT_DYNAMIC) {
					serial_trace("dynamic pheader found\n");
					libdyn = (Elf64_Dyn*) ((
						uint8_t*) (lib->entry
							   + phdr->p_offset));
				} else if (phdr->p_type == PT_LOAD) {
					void* a = (void*) (vxPhysBaseAlloc(
						1 + phdr->p_memsz / 4096));
					uintptr_t allignment_vadr =
						phdr->p_vaddr & ~0xFFF;
					uintptr_t offset_page =
						phdr->p_vaddr - allignment_vadr;

					if ((uint64_t) a % 0x1000 != 0) {
						serial_trace("ERROR: PADDRNOT "
							     "ALIGNMENT "
							     "AT 4KB\n");
					}
					if (phdr->p_vaddr % 0x1000 != 0) {
						serial_trace(
							"ERROR: VADDR NOT "
							"ALIGNMENT AT 4KB\n");
					}
					uint64_t round_up_size =
						(phdr->p_memsz + 0xFFF)
						& ~0xFFF;
					vxMultipleMmap(page_, allignment_vadr,
						       VIRT2PHYS((uint64_t) a),
						       (round_up_size / 4096),
						       0b111);
					// }
					serial_trace("mapping 0x%x to "
						     "0x%x\n",
						     allignment_vadr,
						     VIRT2PHYS((uint64_t) a)
							     + offset_page);
					// paging_reload(VIRT2PHYS((uint64_t)page_));
					// serial_trace("mapped addr : 0x%x\n",
					// vaddr_to_paddr(((uint64_t)page_),
					// phdr->p_vaddr));

					for (uint64_t j = 0; j < phdr->p_memsz;
					     j++) {
						*(uint8_t*) ((uint64_t) a
							     + offset_page
							     + j) =
							*(uint8_t*) ((uint64_t) lib
									     ->entry
								     + phdr->p_offset
								     + j);
					}
				}
				serial_trace("phdr type : %d\n", phdr->p_type);

				phdr = (Elf64_Phdr*) ((uint64_t) phdr
						      + ehdr->e_phentsize);
			}

			// paging_reload (page_);
			// read dynamic section
			if (libdyn != 0) {
				for (int i = 0; libdyn[i].d_tag != DT_NULL;
				     i++) {
					switch (libdyn[i].d_tag) {
					case DT_INIT:
						// case DT_INIT_ARRAY:
						serial_trace(
							"dt init : 0x%x\n",
							libdyn[i].d_un.d_ptr);
						init_func_addr =
							(uint8_t*) (libdyn[i]
									    .d_un
									    .d_ptr);
						// void (*init) ()
						//     = (void (*) ())lib->entry
						//       + libdyn[i].d_un.d_ptr;
						// init ();
						break;
					default:
						break;
					}
				}
			}
		}

		paging_reload((page_t) VIRT2PHYS((uint64_t) page_));
		// relocation
		if (plt_rela != 0) {
			for (int i = 0; i < plt_relz; i++) {
				// void *a = (void *)(phys_base_alloc(1));
				// paging_mmap(page_, plt_rela[i].r_offset,
				//             VIRT2PHYS((uint64_t)a), 0b111);
				// paging_reload(VIRT2PHYS((uint64_t)page_));

				serial_trace("relocation offset : 0x%x\n",
					     plt_rela[i].r_offset);
				// serial_trace("mapped addr : 0x%x\n",
				// vaddr_to_paddr(
				//                                          ((uint64_t)page_),
				//                                          plt_rela[i].r_offset));
				serial_trace("relocation info : 0x%x\n",
					     plt_rela[i].r_info);
				serial_trace("relocation addend : 0x%x\n",
					     plt_rela[i].r_addend);
				serial_trace("relocation type : 0x%x\n",
					     Elf64_R_TYPE(plt_rela[i].r_info));
				Elf64_Sym* sym = &symbols[Elf64_R_SYM(
					plt_rela[i].r_info)];
				char* sym_name =
					(char*) (strtab + sym->st_name);
				serial_trace("relocation symbol : %s\n",
					     sym_name);

				// rellocation proccess
				if (lib_dynsym != 0) {
					for (int k = 0;
					     k < lib_dynsym_size
							 / sizeof(Elf64_Sym);
					     k++) {
						const char* lib_dynsym_name =
							(const char*) (dynstr
								       + lib_dynsym[k]
										 .st_name);
						if (strncmp(lib_dynsym_name,
							    sym_name,
							    strlen(sym_name))
						    == 0) {
							serial_trace(
								"found symbol "
								"%s\n",
								lib_dynsym_name);
							serial_trace(
								"symbol value "
								": "
								"0x%x\n",
								lib_dynsym[k]
									.st_value);
							serial_trace(
								"relocation "
								"value "
								": "
								"0x%x\n",
								lib_dynsym[k].st_value
									+ plt_rela[i]
										  .r_addend);
							serial_trace(
								"mapped addr : "
								"0x%x\n",
								vaddr_to_paddr(
									(page_t) ((
										uint64_t) page_),
									lib_dynsym[k].st_value
										+ plt_rela[i]
											  .r_addend));

							serial_trace(
								"ok 0x%x\n",
								*(uint64_t*) (plt_rela[i]
										      .r_offset));
							*(uint64_t*) (plt_rela[i]
									      .r_offset) =
								(uint64_t) (lib_dynsym[k]
										    .st_value
									    + plt_rela[i]
										      .r_addend);

							break;
						}
					}
				}
			}
		}
	}
	serial_trace("init func addr : 0x%x\n", init_func_addr);
	// signal to IDT that program is running
	is_running_program = 1;

	// TODO: stup correctly stack consider shared library

	uintptr_t s = (uintptr_t) (vxPhysBaseAlloc(2049));
	memset((void*) s, 0, 2049 * 4096);

	char* argva[1] = {"a"};
	char* envpa[1] = {"a"};
	// setup_user_stack (s, 0, 0);
	// mapping to 2GB
	vxMultipleMmap(page_, 256 * GB, VIRT2PHYS((uint64_t) s), 2049, 0b111);

	uint64_t argc = 0;
	while (argv[argc])
		argc++;

	uint64_t aligned_stack = (uintptr_t) 256 * GB + 2048 * 0x1000;
	uint64_t* stack_top = (uint64_t*) ((uintptr_t) s + 2048 * 0x1000);
	aligned_stack =
		setup_user_stack(argc, aligned_stack, stack_top, argv, envp);
	// *()set returning address on stack
	// *(--stack_top) = (uint64_t)0x0240000000;
	// aligned_stack -= sizeof(uintptr_t);

	serial_trace("aligned stack : 0x%x\n", aligned_stack);

	struct program_paramater param = {
		.argv = argv,
		.envp = envp,
		.argc = argc,
	};
	task_add(path, entry, TASK_READY, TASK_PRIORITY_MEDIUM,
		 (page_t) VIRT2PHYS(page_), aligned_stack, param);

	return 1;
}
