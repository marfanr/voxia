#include "procc/proccess.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/executable/elf.h"
#include "libk/math.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "procc/thread.h"
#include "type.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vnode.h"
#include <str.h>

// static pid_t increment_pid = 1;
// static proccess_t* proccess_list;

INIT(Proccess) {}

// TODO: make enum for return error code
int execve(const char* path, char* const* argv, char* const* envp) {
	UNUSED(path);
	UNUSED(argv);
	UNUSED(envp);

	serial2_printf("exec proccess %s ... \n", path);

	dentry_ptr loaded_file_dentry;
	if (resolve_dentry((char*)path, 0, &loaded_file_dentry, 0) != VFS_OK) {
		LOG2_ERROR("Proccess", "executbale %s not found", path);
		return -1;
	}

	if (loaded_file_dentry) {
		serial2_printf("found %s (size %d kb) \n",
		               loaded_file_dentry->name->c_str,
		               loaded_file_dentry->vnode->size / 1024);
	}

	// alloc new page
	// auto page = paging_create_page_directory();
	auto page = paging_get_highest_page_map();

	// loaded into buffer
	auto size = loaded_file_dentry->vnode->size;
	uint8_t* file_buffer = (uint8_t*)kalloc(size);
	if (!file_buffer) {
		LOG2_ERROR("PROCESS", "unable to alloc %d kb", size / 1024);
		dentry_put(loaded_file_dentry);
		return -1;
	}

	auto ops = (vops_file_t*)loaded_file_dentry->vnode->ops;
	if (!ops) {
		LOG_ERROR("PROCESS", "ops is null");
		kfree2(file_buffer);
		dentry_put(loaded_file_dentry);
		return -1;
	}

	ops->read(loaded_file_dentry->vnode, file_buffer, size, 0);

	Elf64_Ehdr ehdr;
	memcopy(&ehdr, (void*)file_buffer, sizeof(Elf64_Ehdr));

	// allocate base addr
	size_t loaded_size = elf_count_load_size(file_buffer);
	LOG_INFO("VOXMO", "loaded size %d (%d kb)", loaded_size,
	         loaded_size / 1024);
	size_t size_4k = ALIGN_UP(1 + loaded_size, BLOCK_SIZE) / BLOCK_SIZE;

	uintptr_t base_addr =
	    vma_lookup_free_vaddr(VMA_REGION_PROCESS, size_4k);
	LOG_INFO("PROCESS", "executable %s has base addr at 0x%x", path,
	         base_addr);

	LOG2_INFO("PROCESS", "found executable type is %d", ehdr.e_type);
	if (ehdr.e_type != ET_DYN && ehdr.e_type != ET_EXEC) {
		LOG2_ERROR("PROCESS", "wrong elf type");
		return -2;
	}

	elf_section_map sh_map = {0};
	elf_section_map_all(file_buffer, &sh_map);

	elf_mmap_got(page, &sh_map, base_addr);

	LOG2_INFO("ELF", "version : %d, ph num : %d", ehdr.e_version,
	          ehdr.e_phnum);
	LOG2_INFO("ELF", "entry : 0x%x", ehdr.e_entry);

	// Elf64_Phdr* phdr = ELF_PTR(Elf64_Phdr, file_buffer, ehdr.e_phoff);
	serial_trace("phdr count %d\n", ehdr.e_phnum);

	uintptr_t base_vaddr = 0;
	Elf64_Phdr* phdr = ELF_PTR(Elf64_Phdr, file_buffer, ehdr.e_phoff);
	for (uint64_t i = 0; i < ehdr.e_phnum; i++) {
		Elf64_Phdr* p = ELF_PTR(Elf64_Phdr, phdr, i * ehdr.e_phentsize);

		if (p->p_vaddr > 0) {
			if (!base_vaddr)
				base_vaddr = p->p_vaddr;
			else
				base_vaddr = min(base_vaddr, p->p_vaddr);
		}
	}

	struct elf_load_mmap_table* mmap_table =
	    (struct elf_load_mmap_table*)kalloc(
	        sizeof(struct elf_load_mmap_table) * ehdr.e_phnum);
	// (void)*mmap_table;

	auto l = elf_load(page, file_buffer, base_addr, base_addr, mmap_table);
	serial2_printf("loaded size %d\n", l);

	uintptr_t temporary_base = 0;
	for (int i = 0; i < ehdr.e_phnum; i++) {
		if (mmap_table[i].mapped) {
			temporary_base = mmap_table[i].vaddr;
			break;
		}
	}

	// TODO: currently bypass
	serial2_printf("new  addr 0x%x\n", base_addr);
	if (!temporary_base) {
		serial2_printf("error temporary addr must be not empty\n");
		// kfree2(mmap_table);
		// dentry_put(loaded_file_dentry);
		// return -2;
	}

	GnuHashHeader gnu_hash;
	Elf64_Shdr* gnu_hash_sym = sh_map.gnuhash;
	elf_gnu_hash_parse(&gnu_hash, gnu_hash_sym, file_buffer);

	Elf64_Dyn* dyn = elf_get_phdr_dynamic(file_buffer);
	if (dyn) {
		LOG_INFO("VOXMO", "dynamic section found at 0x%x", dyn);
		elf_dynamic_map dyn_map = {0};
		elf_dyn_map_all(dyn, file_buffer, &dyn_map);

		LOG_INFO("VOXMO", "strtab found at 0x%x", dyn_map.strtab);
		LOG_INFO("VOXMO", "needed size %d", dyn_map.needed.size);

		elf_relocate_dyn(&dyn_map, base_addr, &gnu_hash, 0);
	}

	elf_call_init_array(&sh_map, base_addr);

	serial2_printf("base vaddr %x %x\n", base_vaddr,
	               (ehdr.e_entry - base_vaddr));

	auto entry_addr = (ehdr.e_entry ) + base_addr;

	create_thread(entry_addr, 1, 2, THREAD_USER);
	serial2_printf(
	    "done setuping executable, now ready to sended to scheduler\n");

	// auto kernel_page = paging_get_highest_page_map();
	// for (int i = 0; i < ehdr.e_phnum; i++) {
	// 	if (mmap_table[i].mapped) {
	// 		vma_unregister(mmap_table[i].vaddr);
	// 		paging_unmap_fill(kernel_page,
	// 		                  mmap_table[i].vaddr +
	// 		                      mmap_table[i].alligned,
	// 		                  mmap_table[i].size);
	// 	}
	// }

	// kfree2(mmap_table);
	dentry_put(loaded_file_dentry);
	return VFS_OK;
}