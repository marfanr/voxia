#include "hal/cpu/paging.h"
#include "libk/math.h"
#include "libk/serial.h"
#include "libk/symbols.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include <libk/executable/elf.h>
#include <str.h>
#include <type.h>
#include <vector.h>

extern void module_loader(uintptr_t addr, uintptr_t stack);
boolean_t elf_has_running = false;
uintptr_t rip_before_run_elf = 0;

uintptr_t elf_find_base_addr(uint8_t* data) {
	Elf64_Ehdr* ehdr =
	    (Elf64_Ehdr*)ASSUME_ALIGNED(data, alignof(Elf64_Ehdr));
	Elf64_Phdr* phdr = (Elf64_Phdr*)ASSUME_ALIGNED(
	    PTR_ADD(data, ehdr->e_phoff), alignof(Elf64_Phdr));

	for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr* p =
		    (Elf64_Phdr*)((uint64_t)phdr + i * ehdr->e_phentsize);
		if (p->p_type == PT_LOAD)
			return p->p_vaddr - p->p_offset;
	}
	return 0;
}

static uint64_t vaddr_to_file_offset(Elf64_Ehdr* ehdr, Elf64_Phdr* phdr,
                                     uint64_t vaddr) {
	for (int i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr* p = &phdr[i];
		if (p->p_type == PT_LOAD && vaddr >= p->p_vaddr &&
		    vaddr < p->p_vaddr + p->p_filesz) {
			return p->p_offset + (vaddr - p->p_vaddr);
		}
	}
	LOG2_WARN("ELF", "vaddr 0x%x not found in any PT_LOAD segment", vaddr);
	return vaddr;
}

Elf64_Dyn* elf_get_phdr_dynamic(uint8_t* data) {
	Elf64_Ehdr* ehdr =
	    (Elf64_Ehdr*)ASSUME_ALIGNED(data, alignof(Elf64_Ehdr));
	Elf64_Phdr* phdr = (Elf64_Phdr*)ASSUME_ALIGNED(
	    PTR_ADD(data, ehdr->e_phoff), alignof(Elf64_Phdr));

	for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr* p =
		    (Elf64_Phdr*)((uint64_t)phdr + i * ehdr->e_phentsize);
		if (p->p_type == PT_DYNAMIC)
			return (Elf64_Dyn*)((uint64_t)data + p->p_offset);
	}
	return NULL;
}

void elf_dyn_map_all(Elf64_Dyn* dyn, uint8_t* data, elf_dynamic_map* map) {
	Elf64_Ehdr* ehdr =
	    (Elf64_Ehdr*)ASSUME_ALIGNED(data, alignof(Elf64_Ehdr));
	Elf64_Phdr* phdr = (Elf64_Phdr*)ASSUME_ALIGNED(
	    PTR_ADD(data, ehdr->e_phoff), alignof(Elf64_Phdr));

	vector_init(&map->needed);

	for (int i = 0; dyn[i].d_tag != DT_NULL; i++) {
		switch (dyn[i].d_tag) {

		case DT_STRTAB: {
			uint64_t off =
			    vaddr_to_file_offset(ehdr, phdr, dyn[i].d_un.d_ptr);
			map->strtab = data + off;
			LOG2_INFO("ELF",
			          "strtab at file offset 0x%x (vaddr 0x%x)",
			          off, dyn[i].d_un.d_ptr);
			break;
		}

		case DT_NEEDED: {
			uint64_t needed = dyn[i].d_un.d_val;
			vector_push_back(&map->needed, needed);
			break;
		}

		case DT_SYMTAB: {
			uint64_t off =
			    vaddr_to_file_offset(ehdr, phdr, dyn[i].d_un.d_ptr);
			map->symbols = (Elf64_Sym*)(void*)(data + off);
			LOG2_INFO("ELF",
			          "symtab at file offset 0x%x (vaddr 0x%x)",
			          off, dyn[i].d_un.d_ptr);
			break;
		}

		case DT_HASH: {
			uint64_t vaddr = dyn[i].d_un.d_ptr;
			uint64_t offset = 0;
			for (int j = 0; j < ehdr->e_phnum; j++) {
				Elf64_Phdr* p = &phdr[j];
				if (vaddr >= p->p_vaddr &&
				    vaddr < p->p_vaddr + p->p_memsz) {
					offset =
					    (vaddr - p->p_vaddr) + p->p_offset;
					break;
				}
			}
			uint32_t* hash = ELF_PTR(uint32_t, data, offset);
			map->symcount = hash[1];
			break;
		}

		case DT_PLTRELSZ:
			map->pltrelsz = dyn[i].d_un.d_val;
			LOG2_INFO("ELF", "pltrel size : %x", map->pltrelsz);
			break;

		case DT_PLTGOT:
			LOG2_INFO("ELF", "pltgot : 0x%x", dyn[i].d_un.d_ptr);
			map->pltgot = (uint64_t)dyn[i].d_un.d_ptr;
			break;

		case DT_JMPREL: {
			uint64_t off =
			    vaddr_to_file_offset(ehdr, phdr, dyn[i].d_un.d_ptr);
			map->jmprel = ELF_PTR(Elf64_Rela, data, off);
			LOG2_INFO("ELF",
			          "jmprel at file offset 0x%x (vaddr 0x%x)",
			          off, dyn[i].d_un.d_ptr);
			break;
		}

		case DT_RELA: {
			uint64_t off =
			    vaddr_to_file_offset(ehdr, phdr, dyn[i].d_un.d_ptr);
			map->rel = ELF_PTR(Elf64_Rela, data, off);
			LOG2_INFO("ELF",
			          "rela at file offset 0x%x (vaddr 0x%x)", off,
			          dyn[i].d_un.d_ptr);
			break;
		}

		case DT_RELASZ:
			map->relasz = dyn[i].d_un.d_val;
			LOG2_INFO("ELF", "relasz : %x", map->relasz);
			break;

		case DT_RELAENT:
			map->relaent = dyn[i].d_un.d_val;
			LOG2_INFO("ELF", "relaent : %x", map->relaent);
			break;

		default:
			break;
		}
	}
}

void elf_section_map_all(uint8_t* data, elf_section_map* map) {
	Elf64_Ehdr* ehdr =
	    (Elf64_Ehdr*)ASSUME_ALIGNED(data, alignof(Elf64_Ehdr));
	Elf64_Shdr* shdr = (Elf64_Shdr*)((uint64_t)data + ehdr->e_shoff);
	Elf64_Shdr* sh_strtab = &shdr[ehdr->e_shstrndx];
	const char* sh_names = (const char*)data + sh_strtab->sh_offset;

	for (uint64_t i = 0; i < ehdr->e_shnum; i++) {
		Elf64_Shdr* s = &shdr[i];
		const char* sec_name = sh_names + s->sh_name;

		switch (s->sh_type) {

		case SHT_SYMTAB:
			map->symtab = s;
			break;

		case SHT_DYNSYM:
			map->dynsym = s;
			break;

		case SHT_GNU_HASH:
			map->gnuhash = s;
			break;

		case SHT_STRTAB:
			if (strncmp(sec_name, ".strtab", 7) == 0)
				map->strtab = s;
			else if (strncmp(sec_name, ".dynstr", 7) == 0)
				map->dynstr = s;
			break;

		case SHT_PROGBITS:
			if (strncmp(sec_name, ".got.plt", 8) == 0) {
				LOG2_INFO("ELF",
				          "found .got.plt at 0x%x, size %d",
				          s->sh_addr, s->sh_size);
				map->gotplt = s;
			} else if (strncmp(sec_name, ".got", 4) == 0) {
				LOG2_INFO("ELF", "found .got at 0x%x, size %d",
				          s->sh_addr, s->sh_size);
				map->got = s;
			}
			break;

		case SHT_RELA:
			break;

		case SHT_INIT_ARRAY:
			map->init_aray = s;
			break;

		case SHT_FINI_ARRAY:
			map->fini_aray = s;
			break;

		default:
			break;
		}
	}
}

uint64_t elf_pflags_to_page_flags(uint64_t p_flags) {
	uint64_t flags = PAGE_USER; // selalu user-accessible

	if (p_flags & PF_R)
		flags |= PAGE_PRESENT;
	if (p_flags & PF_W)
		flags |= PAGE_WRITABLE | PAGE_PRESENT;
	if (p_flags & PF_X)
		flags |= PAGE_PRESENT;
	else
		flags |= PAGE_NO_EXECUTE; // NX jika bukan executable segment

	return flags;
}

void elf_mmap_got(volatile uintptr_t* page, elf_section_map* map,
                  uintptr_t base) {
	if (!map->gotplt)
		return;

	uint64_t start = map->got ? map->got->sh_addr : map->gotplt->sh_addr;
	uint64_t end = map->gotplt->sh_addr + map->gotplt->sh_size;
	uint64_t total = end - start;

	uint64_t aligned_start = ALIGN_DOWN(start, PAGE_SIZE_4KB);
	uintptr_t offset_aligned = start - aligned_start;
	uint64_t total_aligned =
	    ALIGN_UP(total + offset_aligned, PAGE_SIZE_4KB) / PAGE_SIZE_4KB;

	LOG2_INFO("VOXMO", "gotplt found at 0x%x (0x%x), size %d",
	          base + aligned_start, offset_aligned, total_aligned);

	uintptr_t phys_addr = (uintptr_t)phys_base_alloc(total_aligned);
	uint64_t got_flags =
	    PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_NO_EXECUTE;
	paging_multiple_mmap(page, base + aligned_start, phys_addr, total_aligned,
	               got_flags);
}

uintptr_t elf_get_entry(uint8_t* data, uintptr_t base) {
	Elf64_Ehdr* ehdr =
	    (Elf64_Ehdr*)ASSUME_ALIGNED(data, alignof(Elf64_Ehdr));
	return base + ehdr->e_entry;
	}

	size_t elf_load(volatile uintptr_t* page, uint8_t* data,
	uintptr_t temporary_base, uintptr_t base,
	struct elf_load_mmap_table* table) {
	(void)temporary_base;

	Elf64_Ehdr* ehdr =
	    (Elf64_Ehdr*)ASSUME_ALIGNED(data, alignof(Elf64_Ehdr));
	LOG2_INFO("ELF", "version : %d, ph num : %d", ehdr->e_version,
	          ehdr->e_phnum);
	LOG2_INFO("ELF", "entry : 0x%x", ehdr->e_entry);

	Elf64_Phdr* phdr = ELF_PTR(Elf64_Phdr, data, ehdr->e_phoff);

	size_t max_end = 0;

	for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr* p =
		    ELF_PTR(Elf64_Phdr, phdr, i * ehdr->e_phentsize);

		uintptr_t aligned_vaddr = ALIGN_DOWN(p->p_vaddr, BLOCK_SIZE);
		uintptr_t vaddr_offset = p->p_vaddr - aligned_vaddr;
		size_t sz =
		    (vaddr_offset + p->p_memsz + BLOCK_SIZE - 1) / BLOCK_SIZE;

		max_end = max(max_end, p->p_vaddr + sz * BLOCK_SIZE);

		if (p->p_type != PT_LOAD)
			continue;

		uintptr_t phys_alloc = (uintptr_t)phys_base_alloc(sz);
		auto kernel_page = paging_get_highest_page_map();
		auto mmap_flags = elf_pflags_to_page_flags(p->p_flags);

		/* Allocate a temporary virtual address in the current kernel space */
		uintptr_t temp_vaddr = vma_lookup_free_vaddr(get_kernel_vmm_page(), VMA_REGION_B, sz);

		/* Map the physical memory to the temporary virtual address so we can write to it */
		paging_multiple_mmap(kernel_page, temp_vaddr, phys_alloc, sz, PAGE_PRESENT | PAGE_WRITABLE);

		/* Copy data to the temporary virtual address */
		memset((void*)(temp_vaddr + vaddr_offset), 0, p->p_memsz);
		if (p->p_filesz > 0) {
			memcopy((void*)(temp_vaddr + vaddr_offset),
			        (void*)((uint8_t*)data + p->p_offset),
			        p->p_filesz);
		}

		/* Map the physical memory into the TARGET page directory at the correct module base address */
		if (kernel_page != page) {
			paging_multiple_mmap(page, base + aligned_vaddr, phys_alloc, sz, mmap_flags);
		} else {
			/* If we are loading into the kernel page directory, update the permissions of our temp map 
			   and treat it as the final destination */
			paging_multiple_mmap(kernel_page, base + aligned_vaddr, phys_alloc, sz, mmap_flags);

			/* If base+aligned_vaddr is different from temp_vaddr, we need to unmap the temp_vaddr.
			   However, for modules, base is usually 0, and we want them relocated.
			   Actually, 'base' is provided by the caller (likely VMA_REGION_KMODULE).
			   The target is ALWAYS 'base + aligned_vaddr'. */
			if (temp_vaddr != base + aligned_vaddr) {
				paging_multiple_unmap(kernel_page, temp_vaddr, sz);
			}
		}

		if (table) {
			auto t = &table[i];
			t->paddr = phys_alloc;
			t->vaddr = base + aligned_vaddr; 
			t->alligned = aligned_vaddr;
			t->size = sz;
			t->flags = mmap_flags;
			t->mapped = true;
		}

		LOG2_INFO("ELF", "vaddr 0x%x, type %d mapped at 0x%x, flags %b",
		          p->p_vaddr, p->p_type, base + aligned_vaddr, mmap_flags);
	}

	LOG2_INFO("ELF", "module loaded");
	return max_end;
	}


static uintptr_t
elf_resolve_external_symbol(symbols_ptr_vector_t* external_syms,
                            const char* name) {
	if (!external_syms)
		return 0;

	size_t name_len = strlen(name);
	for (size_t j = 0; j < external_syms->size; j++) {
		vector(symbols_item)* item = &external_syms->data[j]->items;
		for (size_t k = 0; k < item->size; k++) {
			if (strlen(item->data[k].name) == name_len &&
			    strncmp(item->data[k].name, name, name_len) == 0)
				return item->data[k].value;
		}
	}
	return 0;
}

static uint64_t* elf_roffset_to_kernel_ptr(uintptr_t r_offset,
                                           struct elf_load_mmap_table* table,
                                           int table_count,
                                           uintptr_t fallback_base) {
	if (!table)
		return (uint64_t*)(fallback_base + r_offset);

	for (int i = 0; i < table_count; i++) {
		if (!table[i].mapped)
			continue;
		uintptr_t seg_vaddr_start = table[i].alligned;
		uintptr_t seg_vaddr_end =
		    seg_vaddr_start + table[i].size * BLOCK_SIZE;
		if (r_offset >= seg_vaddr_start && r_offset < seg_vaddr_end) {
			uintptr_t kernel_segment_base = table[i].vaddr;
			return (uint64_t*)(kernel_segment_base +
			                   (r_offset - seg_vaddr_start));
		}
	}

	return (uint64_t*)(fallback_base + r_offset);
}

static void
elf_relocate_rel(Elf64_Rela* rel, uintptr_t kernel_base, uintptr_t user_base,
                 uint64_t rela_count, uint8_t* strtab, Elf64_Sym* symbols,
                 GnuHashHeader* gnu_hash, symbols_ptr_vector_t* external_syms,
                 struct elf_load_mmap_table* table, int table_count) {

	for (uint64_t i = 0; i < rela_count; i++) {
		uint64_t sym_idx = ELF64_R_SYM(rel[i].r_info);
		uint64_t type = ELF64_R_TYPE(rel[i].r_info);
		Elf64_Sym* symbol = &symbols[sym_idx];
		const char* name = (const char*)(strtab + symbol->st_name);

		Elf64_Sym* lookup = elf_gnu_lookup(name, gnu_hash, symbols,
		                                   (const char*)strtab);
		if (lookup)
			symbol = lookup;

		uint64_t* target = elf_roffset_to_kernel_ptr(
		    rel[i].r_offset, table, table_count, kernel_base);

		switch (type) {

		case R_X86_64_DTPMOD64:
		case R_X86_64_DTPOFF64:
		case R_X86_64_TPOFF64:
		case R_X86_64_TLSGD:
		case R_X86_64_TLSLD:
		case R_X86_64_DTPOFF32:
		case R_X86_64_GOTTPOFF:
		case R_X86_64_TPOFF32:
			LOG2_WARN("ELF", "TLS relocation not implemented");
			break;

		case R_X86_64_RELATIVE:
			*target =
			    (uintptr_t)((intptr_t)user_base + rel[i].r_addend);
			break;

		case R_X86_64_IRELATIVE: {
			uintptr_t resolver_kptr =
			    (uintptr_t)elf_roffset_to_kernel_ptr(
			        (uintptr_t)rel[i].r_addend, table, table_count,
			        kernel_base);

			if (!resolver_kptr) {
				LOG2_WARN("ELF",
				          "IRELATIVE: resolver not found for "
				          "r_addend=0x%x",
				          rel[i].r_addend);
				break;
			}

			uintptr_t (*resolver)(void) =
			    (uintptr_t(*)(void))resolver_kptr;
			uintptr_t result = resolver();

			if (user_base) {
				result += user_base;
			}

			*target = result;

			LOG2_DEBUG("ELF",
			           "IRELATIVE r_offset=0x%x resolver_addr=0x%x",
			           rel[i].r_offset, *target);
			break;
		}

		case R_X86_64_64:
			*target = (uintptr_t)((intptr_t)user_base +
			                      (intptr_t)symbol->st_value +
			                      rel[i].r_addend);
			break;

		case R_X86_64_32: {
			uint32_t value = (uint32_t)((intptr_t)user_base +
			                            (intptr_t)symbol->st_value +
			                            rel[i].r_addend);
			*(uint32_t*)target = value;
			break;
		}

		case R_X86_64_32S: {
			int32_t value = (int32_t)((intptr_t)user_base +
			                          (intptr_t)symbol->st_value +
			                          rel[i].r_addend);
			*(int32_t*)target = value;
			break;
		}

		case R_X86_64_PC32: {
			uintptr_t symbol_addr =
			    (uintptr_t)((intptr_t)user_base +
			                (intptr_t)symbol->st_value +
			                rel[i].r_addend);
			*(uint32_t*)target =
			    (uint32_t)(symbol_addr -
			               (user_base + rel[i].r_offset));
			break;
		}

		case R_X86_64_COPY:
			if (symbol->st_size) {
				uint64_t* src = elf_roffset_to_kernel_ptr(
				    symbol->st_value, table, table_count,
				    kernel_base);
				memcopy((void*)target, (void*)src,
				        symbol->st_size);
			}
			break;

		case R_X86_64_GLOB_DAT: {
			// GLOB_DAT: addend selalu 0 per ABI, ignore r_addend
			uintptr_t value = 0;
			if (symbol->st_value) {
				value = (uintptr_t)((intptr_t)user_base +
				                    (intptr_t)symbol->st_value);
			} else {
				value = elf_resolve_external_symbol(
				    external_syms, name);
				if (!value)
					LOG2_WARN(
					    "ELF",
					    "GLOB_DAT: symbol %s not found",
					    name);
			}
			if (value)
				*target = value;
			break;
		}

		case R_X86_64_JUMP_SLOT: {
			// JUMP_SLOT: lazy binding — set ke alamat PLT stub jika
			// ada, atau langsung resolve
			uintptr_t value = 0;
			if (symbol->st_value) {
				value = (uintptr_t)((intptr_t)user_base +
				                    (intptr_t)symbol->st_value);
			} else {
				value = elf_resolve_external_symbol(
				    external_syms, name);
				if (!value)
					LOG2_WARN(
					    "ELF",
					    "JUMP_SLOT: symbol %s not found",
					    name);
			}
			if (value)
				*target = value;
			break;
		}

		default:
			*target = user_base + symbol->st_value;
			LOG2_DEBUG("ELF",
			           "unhandled reloc type %d at 0x%x -> 0x%x",
			           type, rel[i].r_offset, *target);
			break;
		}
	}
}

void elf_relocate_dyn(elf_dynamic_map* map, uintptr_t kernel_base,
                      uintptr_t user_base, GnuHashHeader* gnu_hash,
                      symbols_ptr_vector_t* external_syms,
                      struct elf_load_mmap_table* table, int table_count) {
	if (!map)
		return;

	serial2_printf("relocating with kernel_base = 0x%x, user_base = 0x%x\n",
	               kernel_base, user_base);

	if (map->rel && map->relasz && map->relaent) {
		LOG2_INFO("VOXMO", "rela found at 0x%x", map->rel);
		uint64_t count = map->relasz / map->relaent;
		LOG2_INFO("VOXMO", "rela count %d", count);
		elf_relocate_rel(map->rel, kernel_base, user_base, count,
		                 map->strtab, map->symbols, gnu_hash,
		                 external_syms, table, table_count);
	}

	if (map->jmprel && map->pltrelsz && map->relaent) {
		LOG2_INFO("VOXMO", "jmprel found at 0x%x", map->jmprel);
		uint64_t count = map->pltrelsz / map->relaent;
		LOG2_INFO("VOXMO", "rela count %d", count);
		elf_relocate_rel(map->jmprel, kernel_base, user_base, count,
		                 map->strtab, map->symbols, gnu_hash,
		                 external_syms, table, table_count);
	}
}

static uint32_t elf_gnu_hash(const char* s) {
	uint32_t h = 5381;
	for (unsigned char c = (unsigned char)*s; c != '\0';
	     c = (unsigned char)*++s)
		h = (h << 5) + h + c;
	return h;
}

static int elf_gnu_maybe_present(GnuHashHeader* gh, uint32_t hash) {
	uint64_t word = gh->bloom[(hash / 64) % gh->bloom_size];
	uint64_t mask =
	    (1ULL << (hash % 64)) | (1ULL << ((hash >> gh->bloom_shift) % 64));
	return (word & mask) == mask;
}

Elf64_Sym* elf_gnu_lookup(const char* name, GnuHashHeader* gh,
                          Elf64_Sym* symtab, const char* strtab) {
	uint32_t hash = elf_gnu_hash(name);

	if (!elf_gnu_maybe_present(gh, hash))
		return NULL;

	uint32_t idx = gh->buckets[hash % gh->nbuckets];
	if (idx < gh->symoffset || idx == 0)
		return NULL;

	size_t name_len = strlen(name);
	for (uint32_t i = idx;; i++) {
		uint32_t h2 = gh->chains[i - gh->symoffset];
		const char* sym_name = strtab + symtab[i].st_name;

		if ((h2 | 1) == (hash | 1) && strlen(sym_name) == name_len &&
		    strncmp(sym_name, name, name_len) == 0)
			return &symtab[i];

		if (h2 & 1)
			break;
	}
	return NULL;
}

void elf_gnu_hash_parse(GnuHashHeader* gnu_hash, Elf64_Shdr* gnu_hash_sym,
                        uint8_t* data) {
	memset(gnu_hash, 0, sizeof(GnuHashHeader));
	if (!gnu_hash_sym)
		return;

	uint8_t* ptr = (uint8_t*)(data + gnu_hash_sym->sh_offset);

	memcopy(&gnu_hash->nbuckets, ptr + 0, sizeof(uint32_t));
	memcopy(&gnu_hash->symoffset, ptr + 4, sizeof(uint32_t));
	memcopy(&gnu_hash->bloom_size, ptr + 8, sizeof(uint32_t));
	memcopy(&gnu_hash->bloom_shift, ptr + 12, sizeof(uint32_t));
	ptr += 16;

	gnu_hash->bloom = (uintptr_t*)ASSUME_ALIGNED(ptr, alignof(uintptr_t));

	ptr += gnu_hash->bloom_size * sizeof(uintptr_t);

	gnu_hash->buckets = (uint32_t*)ASSUME_ALIGNED(ptr, alignof(uint32_t));

	ptr += gnu_hash->nbuckets * sizeof(uint32_t);

	gnu_hash->chains = (uint32_t*)ASSUME_ALIGNED(ptr, alignof(uint32_t));
}

uintptr_t elf_count_load_size(uint8_t* data) {
	Elf64_Ehdr* ehdr =
	    (Elf64_Ehdr*)ASSUME_ALIGNED(data, alignof(Elf64_Ehdr));
	Elf64_Phdr* phdr = ELF_PTR(Elf64_Phdr, data, ehdr->e_phoff);

	LOG2_INFO("ELF", "version : %d, ph num : %d", ehdr->e_version,
	          ehdr->e_phnum);
	size_t max_end = 0;

	for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr* p =
		    ELF_PTR(Elf64_Phdr, phdr, i * ehdr->e_phentsize);
		if (p->p_type != PT_LOAD)
			continue;
		uintptr_t seg_end =
		    ALIGN_UP(p->p_vaddr + p->p_memsz, BLOCK_SIZE);
		max_end = max(max_end, seg_end);
	}

	LOG2_INFO("ELF", "module load size: %d bytes (%d kb)", max_end,
	          max_end / 1024);
	return max_end;
}

void elf_get_symbol(const char* sym_name, uintptr_t base, elf_section_map* map,
                    uint8_t* data, symbols_ptr_vector_t* syms,
                    boolean_t skip_empty_val) {
	if (!map->symtab || !map->strtab)
		return;

	Elf64_Sym* symtab =
	    (Elf64_Sym*)((uint64_t)data + map->symtab->sh_offset);
	const char* strtab_data = (const char*)(data + map->strtab->sh_offset);
	uint64_t sym_count = map->symtab->sh_size / sizeof(Elf64_Sym);

	symbols* s = (symbols*)kalloc(sizeof(symbols));
	vector_init(&s->items);
	s->name = sym_name;

	for (uint64_t i = 0; i < sym_count; i++) {
		if (skip_empty_val && symtab[i].st_value == 0)
			continue;

		symbols_item item;
		item.name = strtab_data + symtab[i].st_name;
		item.value = base + symtab[i].st_value;
		vector_push_back(&s->items, item);
	}
	vector_push_back(syms, s);
}

void elf_call_init_array(elf_section_map* map, uintptr_t base) {
	if (!map->init_aray) {
		return;
	}

	Elf64_Shdr* init_aray = map->init_aray;

	ctor_t* arr = (ctor_t*)(base + init_aray->sh_addr);
	size_t count = init_aray->sh_size / sizeof(void*);

	LOG2_INFO("ELF", "lib init array count %d", count);
	for (size_t i = 0; i < count; i++) {
		if (arr[i]) {
			LOG2_DEBUG("ELF", "load .init array %x", arr[i]);
			arr[i]();
		}
	}
}

void elf_call_init_array_with_table(elf_section_map* map,
                                    struct elf_load_mmap_table* table,
                                    int table_count) {

	(void)map;
	(void)table;
	(void)table_count;

	if (!map->init_aray)
		return;

	size_t count = map->init_aray->sh_size / sizeof(void*);
	serial2_printf("init array count %d\n", count);
	for (size_t i = 0; i < count; i++) {
		ctor_t* fn_ptr = (ctor_t*)elf_roffset_to_kernel_ptr(
		    map->init_aray->sh_addr + i * sizeof(void*), table,
		    table_count, 0);

		serial2_printf("sh_addr 0x%x\n", map->init_aray->sh_addr);
		serial2_printf("fn_ptr: 0x%x\n", fn_ptr);
		serial2_printf("fn_ptr contain 0x%x\n", *fn_ptr);

		if (fn_ptr && *fn_ptr) {
			auto fn = (void*)elf_roffset_to_kernel_ptr(
			    (uintptr_t)*fn_ptr, table, table_count, 0);
			serial2_printf("mapped fn at 0x%x\n", fn);
			((void(*)())fn)();
		}
	}
}

uintptr_t elf_find_symbol(const char* name, GnuHashHeader* gnuhash,
                          uintptr_t base, elf_section_map* map, uint8_t* data) {
	if (!map->symtab || !map->strtab)
		return 0;

	const char* strtab_data =
	    (const char*)((uint64_t)data + map->strtab->sh_offset);
	Elf64_Sym* symtab =
	    (Elf64_Sym*)((uint64_t)data + map->symtab->sh_offset);

	Elf64_Sym* sym = elf_gnu_lookup(name, gnuhash, symtab, strtab_data);
	if (sym && sym->st_value) {
		LOG2_INFO("ELF", "sym found %s by gnu hash 0x%x",
		          strtab_data + sym->st_name, base + sym->st_value);
		return base + sym->st_value;
	}

	size_t name_len = strlen(name);
	size_t sym_count = map->symtab->sh_size / sizeof(Elf64_Sym);
	for (size_t i = 0; i < sym_count; i++) {
		const char* sname = strtab_data + symtab[i].st_name;
		if (strlen(sname) == name_len &&
		    strncmp(sname, name, name_len) == 0) {
			LOG2_INFO("ELF", "sym found %s 0x%x", sname,
			          symtab[i].st_value);
			return base + symtab[i].st_value;
		}
	}

	LOG2_WARN("ELF", "sym %s not found", name);
	return 0;
}