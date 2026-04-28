#include "hal/cpu/paging.h"
#include "libk/math.h"
#include "libk/serial.h"
#include "libk/symbols.h"
#include "libk/type.h"
#include "libk/vector.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include <libk/executable/elf.h>
#include <libk/str.h>

#define ELF64_R_SYM(i) ((i) >> 32) // ambil 32 bit atas → index simbol
#define ELF64_R_TYPE(i) ((i) & 0xffffffffL) // ambil 32 bit bawah → tipe
#define ELF64_R_INFO(s, t) (((Elf64_Xword)(s) << 32) + (Elf64_Xword)(t))

extern void module_loader(uintptr_t addr, uintptr_t stack);
boolean_t elf_has_running = false;
uintptr_t rip_before_run_elf = 0;

uintptr_t elf_find_base_addr(uint8_t* data) {
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
	Elf64_Phdr* phdr = (Elf64_Phdr*)((uint64_t)data + ehdr->e_phoff);
	for (uint64_t i = 0; i < ehdr->e_shnum; i++) {
		Elf64_Phdr* p =
		    (Elf64_Phdr*)((uint64_t)phdr + i * ehdr->e_phentsize);
		if (p->p_type == PT_LOAD) {
			return p->p_vaddr - p->p_offset;
		}
	}
}

Elf64_Dyn* elf_get_phdr_dynamic(uint8_t* data) {
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
	Elf64_Phdr* phdr = (Elf64_Phdr*)((uint64_t)data + ehdr->e_phoff);
	for (uint64_t i = 0; i < ehdr->e_shnum; i++) {
		Elf64_Phdr* p =
		    (Elf64_Phdr*)((uint64_t)phdr + i * ehdr->e_phentsize);
		if (p->p_type == PT_DYNAMIC) {
			return (Elf64_Dyn*)((uint64_t)data + p->p_offset);
		}
	}
	return NULL;
}

void elf_dyn_map_all(Elf64_Dyn* dyn, uint8_t* data, elf_dynamic_map* map) {
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
	Elf64_Phdr* phdr = (Elf64_Phdr*)((uint64_t)data + ehdr->e_phoff);
	vector_init(&map->needed);
	for (int i = 0; dyn[i].d_tag != DT_NULL; i++) {
		switch (dyn[i].d_tag) {
		case DT_STRTAB:
			map->strtab =
			    (uint8_t*)((uint64_t)data + dyn[i].d_un.d_ptr);
			break;

		case DT_NEEDED:
			uint64_t needed = dyn[i].d_un.d_val;
			vector_push_back(&map->needed, needed);
			break;

		case DT_SYMTAB:
			map->symbols =
			    (Elf64_Sym*)((uint64_t)data + dyn[i].d_un.d_ptr);
			break;

		case DT_HASH:
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

			uint32_t* hash = (uint32_t*)(data + offset);
			map->symcount = hash[1];
			break;

		case DT_PLTRELSZ:
			map->pltrelsz = dyn[i].d_un.d_val;
			LOG2_INFO("ELF", "pltrel size : %x", map->pltrelsz);
			break;

		case DT_PLTGOT:
			LOG2_INFO("ELF", "pltgot : 0x%x", dyn[i].d_un.d_ptr);
			map->pltgot = (uint64_t)dyn[i].d_un.d_ptr;
			break;

		case DT_JMPREL:
			map->jmprel = (Elf64_Rela*)(data + dyn[i].d_un.d_ptr);
			LOG2_INFO("ELF", "jmprel : 0x%x", map->jmprel);
			break;

		case DT_RELA:
			LOG2_INFO("ELF", "found rela at : 0x%x",
			          data + dyn[i].d_un.d_ptr);
			map->rel = (Elf64_Rela*)(data + dyn[i].d_un.d_ptr);
			break;

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
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
	Elf64_Shdr* shdr = (Elf64_Shdr*)((uint64_t)data + ehdr->e_shoff);
	Elf64_Shdr* sh_strtab =
	    &shdr[ehdr->e_shstrndx]; // string table untuk nama section
	const char* sh_names = (const char*)data + sh_strtab->sh_offset;
	for (uint64_t i = 0; i < ehdr->e_shnum; i++) {
		Elf64_Shdr* s = (Elf64_Shdr*)&shdr[i];
		const char* sec_name = sh_names + s->sh_name;
		switch (s->sh_type) {
		case SHT_SYMTAB:
			map->symtab = s;
			break;

		case SHT_GNU_HASH:
			map->gnuhash = s;
			break;

		case SHT_STRTAB:
			if (strncmp(sec_name, ".strtab", 7) == 0) {
				map->strtab = s;
			}
			break;

		case SHT_PROGBITS:
			if (strncmp(sec_name, ".got.plt", 8) == 0) {
				LOG2_INFO("ELF",
				          "found .got.plt at 0x%x, size %d",
				          s->sh_addr, s->sh_size);
				map->gotplt = s;
			} else if (strncmp(sec_name, ".got", 4) == 0) {
				LOG2_INFO("ELF", "found .plt at 0x%x, size %d",
				          s->sh_addr, s->sh_size);
				map->got = s;
			}

		case SHT_RELA:
			break;

		case SHT_INIT_ARRAY:
			map->init_aray = s;
			break;

		case SHT_FINI_ARRAY:
			map->fini_aray = s;
			break;
		}
	}
}

void elf_mmap_got(elf_section_map* map, uintptr_t base) {
	if (map->gotplt) {
		uint64_t start =
		    map->got ? map->got->sh_addr : map->gotplt->sh_addr;
		uint64_t end = map->gotplt->sh_addr + map->gotplt->sh_size;
		uint64_t total = end - start;
		uint64_t alligned_start = ALIGN_DOWN(start, PAGE_SIZE);
		uintptr_t offset_aligned = start - alligned_start;
		uint64_t total_aligned = ALIGN_UP(total, PAGE_SIZE) / PAGE_SIZE;

		LOG2_INFO("VOXMO", "gotplt found at 0x%x (0x%x), size %d",
		          base + alligned_start, offset_aligned, total_aligned);

		uintptr_t phys_addr = (uintptr_t)vxPhysBaseAlloc(total_aligned);
		vxMultipleMmap(paging_get_highest_page_map(),
		               base + alligned_start, phys_addr, total_aligned,
		               0b111);
		paging_reload(paging_get_highest_page_map());
		map->gotplt->sh_addr = base + alligned_start + offset_aligned;
	}
}

uintptr_t elf_get_entry(uint8_t* data, uintptr_t base) {
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
	return base + ehdr->e_entry;
}

size_t elf_load(uint8_t* data, uintptr_t base) {
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
	LOG2_INFO("ELF", "version : %d, ph num : %d", ehdr->e_version,
	          ehdr->e_phnum);
	LOG2_INFO("ELF", "entry : 0x%x", ehdr->e_entry);

	Elf64_Phdr* phdr = (Elf64_Phdr*)(data + ehdr->e_phoff);

	size_t max_end = 0;

	// find PT_LOAD
	for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr* p =
		    (Elf64_Phdr*)((uint8_t*)phdr + i * ehdr->e_phentsize);
		uintptr_t aligned_vaddr = ALIGN_DOWN(p->p_vaddr, BLOCK_SIZE);
		uintptr_t vaddr_offset = p->p_vaddr - aligned_vaddr;
		size_t sz =
		    (vaddr_offset + p->p_memsz + BLOCK_SIZE - 1) / BLOCK_SIZE;
		max_end = max(max_end, p->p_vaddr + sz * BLOCK_SIZE);

		if (p->p_type != PT_LOAD)
			continue;

		LOG2_INFO("ELF", "vaddr 0x%x, type %d", p->p_vaddr, p->p_type);

		uintptr_t a = (uintptr_t)vxPhysBaseAlloc(sz);
		vxMultipleMmap(paging_get_highest_page_map(),
		               base + aligned_vaddr, (uintptr_t)a, sz, 0b111);
		paging_reload(paging_get_highest_page_map());
		memcopy((void*)(base + p->p_vaddr),
		        (void*)((uint8_t*)data + p->p_offset), p->p_filesz);
	}

	LOG2_INFO("ELF", "module loaded");
	return max_end;
}

static uintptr_t
elf_resolve_external_symbol(symbols_ptr_vector_t* external_syms,
                            const char* name) {
	uintptr_t result = 0;
	if (external_syms) {
		for (size_t j = 0; j < external_syms->size; j++) {
			vector(symbols_item)* item =
			    &external_syms->data[j]->items;
			for (size_t k = 0; k < item->size; k++) {
				// LOG_DEBUG("ELF", "checking external symbol
				// %s", item->data[k].name);
				if (strncmp(item->data[k].name, name,
				            strlen(name)) == 0) {
					result = item->data[k].value;
					return result;
				}
			}
		}
	}

	return result;
}

void elf_relocate_rel(Elf64_Rela* rel, uintptr_t base, uint64_t rela_count,
                      uint8_t* strtab, Elf64_Sym* symbols,
                      GnuHashHeader* gnu_hash,
                      symbols_ptr_vector_t* external_syms) {
	for (uint64_t i = 0; i < rela_count; i++) {
		Elf64_Sym* symbol = &symbols[Elf64_R_SYM(rel[i].r_info)];
		const char* name = (const char*)(strtab + symbol->st_name);
		// LOG2_INFO("ELF", "[offset 0x%x] [%d] symbol %s, value 0x%x,
		// size %d", rel[i].r_offset, i,
		//  name, symbol->st_value, symbol->st_size);

		Elf64_Sym* lookup_sym = elf_gnu_lookup(name, gnu_hash, symbols,
		                                       (const char*)strtab);
		if (lookup_sym) {
			// LOG_DEBUG("ELF", "found lookup at gnu symbl %s",
			// name);
			symbol = lookup_sym;
		}

		uint64_t type = Elf64_R_TYPE(rel[i].r_info);

		// TODO: support another type
		switch (type) {
		case R_X86_64_DTPMOD64:
		case R_X86_64_DTPOFF64:
		case R_X86_64_TPOFF64:
		case R_X86_64_TLSGD:
		case R_X86_64_TLSLD:
		case R_X86_64_DTPOFF32:
		case R_X86_64_GOTTPOFF:
		case R_X86_64_TPOFF32:
			LOG_WARN("ELF", "TLS relocation not implemented");
			break;

		case R_X86_64_RELATIVE: {
			uint64_t* relloc_addr =
			    (uint64_t*)(base + rel[i].r_offset);
			*relloc_addr = base + *relloc_addr;
			break;
		}

		case R_X86_64_64: {
			uint64_t* relloc_addr =
			    (uint64_t*)(base + rel[i].r_offset);
			*relloc_addr =
			    base + rel[i].r_addend + symbol->st_value;
			break;
		}

		case R_X86_64_COPY:
			memcopy(
			    (void*)(base + rel[i].r_offset),
			    (void*)(base +
			            symbol->st_value), // atau alamat eksternal
			    symbol->st_size);
			break;

		case R_X86_64_GLOB_DAT:
		case R_X86_64_JUMP_SLOT: {
			uintptr_t value =
			    base + symbol->st_value + rel[i].r_addend;
			if (!symbol->st_value) {
				LOG_DEBUG("ELF", "resolving external symbol %s",
				          name);
				value = elf_resolve_external_symbol(
				    external_syms, name);
			}
			if (!value) {
				LOG_WARN("ELF", "symbol %s not found", name);
				break;
			}
			*(uint64_t*)(base + rel[i].r_offset) = value;
			// LOG_DEBUG("ELF",
			//           "update gotplt at 0x%x (0x%x + %x) to
			//           0x%x", base + rel[i].r_offset, base,
			//           rel[i].r_offset, value);

			break;
		}

		default: {
			*(uint64_t*)(base + rel[i].r_offset) =
			    base + symbol->st_value;
			LOG_DEBUG("ELF",
			          "update gotplt at 0x%x (0x%x + x) to 0x%x",
			          base + rel[i].r_offset, base, rel[i].r_offset,
			          base + symbol->st_value);
			break;
		}
		}
	}
}

void elf_relocate_dyn(elf_dynamic_map* map, uintptr_t base,
                      GnuHashHeader* gnu_hash,
                      symbols_ptr_vector_t* external_syms) {
	if (!map) {
		return;
	}

	if (map->rel) {
		LOG2_INFO("VOXMO", "rela found at 0x%x", map->rel);
		uint64_t rela_count = map->relasz / map->relaent;
		LOG2_INFO("VOXMO", "rela count %d", rela_count);
		elf_relocate_rel(map->rel, base, rela_count, map->strtab,
		                 map->symbols, gnu_hash, external_syms);
	}

	if (map->jmprel && map->pltrelsz && map->relaent) {
		LOG2_INFO("VOXMO", "jmprel found at 0x%x", map->jmprel);
		uint64_t rela_count = map->pltrelsz / map->relaent;
		LOG2_INFO("VOXMO", "rela count %d", rela_count);
		elf_relocate_rel(map->jmprel, base, rela_count, map->strtab,
		                 map->symbols, gnu_hash, external_syms);
	}
}

/* GNU HASH */
uint32_t elf_gnu_hash(const char* s) {
	uint32_t h = 5381;
	for (unsigned char c = *s; c != '\0'; c = *++s)
		h = (h << 5) + h + c; // h * 33 + c
	return h;
}

uintptr_t elf_count_load_size(uint8_t* data) {
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
	Elf64_Phdr* phdr = (Elf64_Phdr*)(data + ehdr->e_phoff);
	size_t max_end = 0;

	// find PT_LOAD
	for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr* p =
		    (Elf64_Phdr*)((uint8_t*)phdr + i * ehdr->e_phentsize);
		uintptr_t aligned_vaddr = ALIGN_DOWN(p->p_vaddr, BLOCK_SIZE);
		uintptr_t vaddr_offset = p->p_vaddr - aligned_vaddr;
		size_t sz =
		    (vaddr_offset + p->p_memsz + BLOCK_SIZE - 1) / BLOCK_SIZE;
		max_end = max(max_end, p->p_vaddr + sz * BLOCK_SIZE);

		if (p->p_type != PT_LOAD)
			continue;
	}

	LOG2_INFO("ELF", "module loaded");
	return max_end;
}

int elf_gnu_maybe_present(GnuHashHeader* gh, uint32_t hash) {
	// Setiap elemen bloom adalah 64-bit (Elf64_Addr)
	uint64_t word = gh->bloom[(hash / 64) % gh->bloom_size];
	uint64_t mask =
	    (1ULL << (hash % 64)) | (1ULL << ((hash >> gh->bloom_shift) % 64));
	return (word & mask) == mask;
}

Elf64_Sym* elf_gnu_lookup(const char* name, GnuHashHeader* gh,
                          Elf64_Sym* symtab, const char* strtab) {
	uint32_t hash = elf_gnu_hash(name);

	// 1. Bloom filter check
	if (!elf_gnu_maybe_present(gh, hash))
		return NULL;

	// 2. Ambil index awal dari bucket
	uint32_t idx = gh->buckets[hash % gh->nbuckets];
	if (idx < gh->symoffset || idx == 0)
		return NULL; // tidak ada simbol di bucket ini

	// 3. Loop di chain[]
	for (uint32_t i = idx;; i++) {
		uint32_t h2 = gh->chains[i - gh->symoffset];

		// Bandingkan hash tanpa bit terakhir
		if ((h2 | 1) == (hash | 1)) {
			const char* sym_name = strtab + symtab[i].st_name;
			if (strncmp(sym_name, name, strlen(sym_name)) == 0)
				return &symtab[i]; // simbol ditemukan
		}

		// bit terakhir = akhir chain
		if (h2 & 1)
			break;
	}
	return NULL;
}

void elf_gnu_hash_parse(GnuHashHeader* gnu_hash, Elf64_Shdr* gnu_hash_sym,
                        uint8_t* data) {
	if (gnu_hash_sym) {
		uint8_t* ptr = (uint8_t*)(data + gnu_hash_sym->sh_offset);

		gnu_hash->nbuckets = *(uint32_t*)(ptr + 0);
		gnu_hash->symoffset = *(uint32_t*)(ptr + 4);
		gnu_hash->bloom_size = *(uint32_t*)(ptr + 8);
		gnu_hash->bloom_shift = *(uint32_t*)(ptr + 12);

		ptr += 16; // lewati header 4x u32

		gnu_hash->bloom = (uintptr_t*)ptr;
		ptr += gnu_hash->bloom_size * sizeof(uintptr_t);

		gnu_hash->buckets = (uint32_t*)ptr;
		ptr += gnu_hash->nbuckets * sizeof(uint32_t);

		gnu_hash->chains = (uint32_t*)ptr;
	}
}

// symbol
void elf_get_symbol(const char* sym_name, uintptr_t base, elf_section_map* map,
                    uint8_t* data, symbols_ptr_vector_t* syms,
                    boolean_t skip_empty_val) {
	if (!map->symtab) {
		return;
	}

	Elf64_Sym* symtab =
	    (Elf64_Sym*)((uint64_t)data + map->symtab->sh_offset);
	Elf64_Shdr* strtab = map->strtab;
	if (!symtab || !strtab) {
		return;
	}

	const char* strtab_data = (const char*)(data + strtab->sh_offset);
	uint64_t sym_count = map->symtab->sh_size / sizeof(Elf64_Sym);

	symbols* s = (symbols*)kalloc(sizeof(symbols));
	vector_init(&s->items);
	s->name = sym_name;
	for (uint64_t i = 0; i < sym_count; i++) {
		if (skip_empty_val && symtab[i].st_value == 0) {
			continue;
		}

		const char* name =
		    (const char*)(strtab_data + symtab[i].st_name);
		// LOG2_INFO("ELF", "[%d] name=%s, value=0x%x size=%d", i, name,
		// base + symtab[i].st_value,
		//          symtab[i].st_size);

		symbols_item item;
		item.name = name;
		item.value = base + symtab[i].st_value;
		vector_push_back(&s->items, item);
	}
	vector_push_back(syms, s);
}

void elf_call_init_array(elf_section_map* map, uintptr_t base) {
	if (map->init_aray) {
		Elf64_Shdr* init_aray = map->init_aray;
		ctor_t* arr = (ctor_t*)(base + init_aray->sh_addr);
		size_t count = init_aray->sh_size / sizeof(void*);
		LOG2_INFO("VOXMO", "lib init array count %d", count);
		for (size_t i = 0; i < count; i++) {
			if (arr[i]) {
				LOG2_DEBUG("VOXMO", "load .init array %x",
				           arr[i]);
				((ctor_t)((uintptr_t)arr[i]))();
			}
		}
	}
}

uintptr_t elf_find_symbol(const char* name, GnuHashHeader* gnuhash,
                          uintptr_t base, elf_section_map* map, uint8_t* data) {
	const char* strtab_data =
	    (const char*)((uint64_t)data + map->strtab->sh_offset);
	Elf64_Sym* symtab =
	    (Elf64_Sym*)((uint64_t)data + map->symtab->sh_offset);

	Elf64_Sym* sym = elf_gnu_lookup(name, gnuhash, symtab, strtab_data);

	if (sym) {
		if (sym->st_value) {
			LOG2_INFO("ELF", "sym found %s by gnu hash 0x%x",
			          strtab_data + sym->st_name,
			          base + sym->st_value);
			return base + sym->st_value;
		}
	}
	size_t sym_count = map->symtab->sh_size / sizeof(Elf64_Sym);
	for (size_t i = 0; i < sym_count; i++) {
		if (strncmp(strtab_data + symtab[i].st_name, name,
		            strlen(name)) == 0) {
			LOG2_INFO("ELF", "sym found %s 0x%x",
			          strtab_data + symtab[i].st_name,
			          symtab[i].st_value);
			return base + symtab[i].st_value;
		}
	}
	LOG_WARN("ELF", "sym %s not found", name);
	return 0;
}