#include "procc/process.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/executable/elf.h"
#include "libk/math.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/slab.h"
#include "memory/vm_manager.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include "string.h"
#include "sys/fd.h"
#include "tty/tty.h"
#include "type.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vnode.h"
#include <hal/cpu/core.h>
#include <str.h>
#include <sys/syscall.h>

static struct slab_cache* process_cache = 0;
static uint8_t* pid_bitmap = 0;
static dentry_ptr process_dentry = 0;
__attribute__((unused)) static struct process_head process_bucket[256] = {0};
static process_t* _process_list = 0;

INIT(Process) {
	vxCreateSlabCache(&process_cache, "process", sizeof(process_t), 0, 0);
	pid_bitmap = (uint8_t*)kalloc(MAX_PID_ALLOWED / 8);
	memset(pid_bitmap, 0, MAX_PID_ALLOWED / 8);
	vxnamei("/proc", &process_dentry);
}

#define AT_NULL 0
#define AT_IGNORE 1
#define AT_EXECFD 2  /* File descriptor from program */
#define AT_PHDR 3    /* Program Headers Address from main executable */
#define AT_PHENT 4   /* program heder entry size */
#define AT_PHNUM 5   /* Program Headers num*/
#define AT_PAGESZ 6  /* page size (4096) */
#define AT_BASE 7    /* Base address dari interpreter (ld.so) jika ada */
#define AT_FLAGS 8   /* Flags */
#define AT_ENTRY 9   /* Entry point dari executable utama */
#define AT_NOTELF 10 /* Program not ELF */

#define AT_UID 11  /* Real User ID */
#define AT_EUID 12 /* Effective User ID */
#define AT_GID 13  /* Real Group ID */
#define AT_EGID 14 /* Effective Group ID */

// --- Hardware & Platform ---
#define AT_PLATFORM 15 /* String CPU format (ex: "x86_64") */
#define AT_HWCAP 16    /* Bitmask CPU capability */
#define AT_CLKTCK 17   /* clock freq, for times() */

#define AT_SECURE                                                              \
	23           /* secure mode (boolean 1/0), used for setuid/setgid      \
	              */
#define AT_RANDOM 25 /* Pointer into 16 byte random for stack canary/ASLR */
#define AT_EXECFN 31 /* String path name executbale file */
#define AT_SYSINFO_EHDR 33

static uintptr_t elf_prepare_stack(uintptr_t stack_top_kernel,
                                   uintptr_t stack_top_user, int argc,
                                   char* const* argv, char* const* envp,
                                   Elf64_Ehdr* ehdr, uintptr_t entry_addr,
                                   uintptr_t phdr_vaddr,
                                   uintptr_t interp_base_addr) {
	uint8_t* stack_top = (uint8_t*)stack_top_kernel;
	uint8_t* sp_ptr = stack_top;

	int envc = 0;
	if (envp) {
		while (envp[envc])
			envc++;
	}

	uintptr_t* envp_user =
	    (uintptr_t*)kalloc(sizeof(uintptr_t) * (size_t)(envc + 1));
	for (int i = envc - 1; i >= 0; i--) {
		size_t len = strlen(envp[i]) + 1;
		sp_ptr -= len;
		memcopy(sp_ptr, (void*)envp[i], len);
		envp_user[i] = stack_top_user - (uintptr_t)(stack_top - sp_ptr);
	}
	envp_user[envc] = 0;

	uintptr_t* argv_user =
	    (uintptr_t*)kalloc(sizeof(uintptr_t) * (size_t)(argc + 1));
	for (int i = argc - 1; i >= 0; i--) {
		size_t len = strlen(argv[i]) + 1;
		sp_ptr -= len;
		memcopy(sp_ptr, (void*)argv[i], len);
		argv_user[i] = stack_top_user - (uintptr_t)(stack_top - sp_ptr);
	}
	argv_user[argc] = 0;

	uint8_t random_bytes[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE,
	                            0xBA, 0xBE, 0x12, 0x34, 0x56, 0x78,
	                            0x9A, 0xBC, 0xDE, 0xF0};
	sp_ptr -= 16;
	memcopy(sp_ptr, random_bytes, 16);
	uintptr_t random_user_addr =
	    stack_top_user - (uintptr_t)(stack_top - sp_ptr);

	uint64_t* sp = (uint64_t*)((uintptr_t)sp_ptr & ~(uintptr_t)15);

	*(--sp) = 0;
	*(--sp) = AT_NULL;
	*(--sp) = entry_addr;
	*(--sp) = AT_ENTRY;
	*(--sp) = random_user_addr;
	*(--sp) = AT_RANDOM;
	*(--sp) = 0;
	*(--sp) = AT_HWCAP;
	*(--sp) = 0;
	*(--sp) = AT_SECURE;
	*(--sp) = 1000;
	*(--sp) = AT_UID;
	*(--sp) = 1000;
	*(--sp) = AT_GID;
	*(--sp) = 1000;
	*(--sp) = AT_EGID;
	*(--sp) = 1000;
	*(--sp) = AT_EUID;

	if (interp_base_addr) {
		*(--sp) = interp_base_addr;
		*(--sp) = AT_BASE;
	}

	*(--sp) = 0x1000;
	*(--sp) = AT_PAGESZ;
	*(--sp) = ehdr->e_phnum;
	*(--sp) = AT_PHNUM;
	*(--sp) = ehdr->e_phentsize;
	*(--sp) = AT_PHENT;
	*(--sp) = phdr_vaddr;
	*(--sp) = AT_PHDR;

	*(--sp) = 0;
	for (int i = envc - 1; i >= 0; i--) {
		*(--sp) = envp_user[i];
	}

	*(--sp) = 0;
	for (int i = argc - 1; i >= 0; i--) {
		*(--sp) = argv_user[i];
	}

	*(--sp) = (uint64_t)argc;

	kfree2(envp_user);
	kfree2(argv_user);

	return stack_top_user - (uintptr_t)(stack_top - (uint8_t*)sp);
}

int execve(const char* path, char* const* argv, char* const* envp) {
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

	auto page = paging_create_page_directory();
	paging_setup(page);
	serial2_printf("new page pml4 %p\n", (uintptr_t)page);

	size_t size = 0;
	uint8_t* file_buffer = 0;
	if (loaded_file_dentry->vnode->type == VNODE_TYPE_FILE) {
		size = loaded_file_dentry->vnode->size;
		file_buffer = (uint8_t*)kalloc(size);
		if (!file_buffer) {
			dentry_put(loaded_file_dentry);
			return -1;
		}

		auto ops = (vops_file_t*)loaded_file_dentry->vnode->ops;
		ops->read(loaded_file_dentry->vnode, file_buffer, size, 0);
	} else if (loaded_file_dentry->vnode->type == VNODE_TYPE_LNK) {
		char link_path[255];
		auto link_ops = (vops_lnk_t*)loaded_file_dentry->vnode->ops;
		link_ops->readlink(loaded_file_dentry->vnode, link_path, 255);

		if (resolve_dentry(link_path, loaded_file_dentry->parent,
		                   &loaded_file_dentry, 0) != VFS_OK) {
			dentry_put(loaded_file_dentry);
			return -1;
		}

		size = loaded_file_dentry->vnode->size;
		file_buffer = (uint8_t*)kalloc(size);
		if (!file_buffer) {
			dentry_put(loaded_file_dentry);
			return -1;
		}

		auto ops = (vops_file_t*)loaded_file_dentry->vnode->ops;
		ops->read(loaded_file_dentry->vnode, file_buffer, size, 0);

	} else {
		LOG2_ERROR("PROCESS", "unsupported file type");
		dentry_put(loaded_file_dentry);
		return -1;
	}

	Elf64_Ehdr ehdr;
	memcopy(&ehdr, (void*)file_buffer, sizeof(Elf64_Ehdr));

	// TODO: validate elf
	if (ehdr.e_ident[0] != ELFMAG0 || ehdr.e_ident[1] != ELFMAG1 ||
	    ehdr.e_ident[2] != ELFMAG2 || ehdr.e_ident[3] != ELFMAG3) {
		LOG2_ERROR("PROCESS", "invalid elf file");
		kfree2(file_buffer);
		dentry_put(loaded_file_dentry);
		return -1;
	}

	size_t loaded_size = elf_count_load_size(file_buffer);
	LOG2_INFO("PROCESS", "loaded size %d (%d kb)", loaded_size,
	          loaded_size / 1024);
	size_t size_4k = ALIGN_UP(1 + loaded_size, BLOCK_SIZE) / BLOCK_SIZE;

	uintptr_t base_addr = vma_lookup_free_vaddr(
	    get_kernel_vmm_page(), VMA_REGION_PROCESS, size_4k);
	LOG2_INFO("PROCESS", "executable %s has base addr at 0x%x", path,
	          base_addr);

	LOG2_INFO("PROCESS", "found executable type is %d", ehdr.e_type);
	if (ehdr.e_type != ET_DYN && ehdr.e_type != ET_EXEC) {
		LOG2_ERROR("PROCESS", "wrong elf type");
		return -2;
	}

	if (ehdr.e_type == ET_EXEC)
		base_addr = 0;

	elf_section_map sh_map = {0};
	elf_section_map_all(file_buffer, &sh_map);

	LOG2_INFO("ELF", "version : %d, ph num : %d", ehdr.e_version,
	          ehdr.e_phnum);
	LOG2_INFO("ELF", "entry : 0x%x", ehdr.e_entry);
	serial2_printf("phdr count %d\n", ehdr.e_phnum);

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

	// find interp
	dentry_ptr interp_dentry = 0;

	for (uint64_t i = 0; i < ehdr.e_phnum; i++) {
		Elf64_Phdr* p = ELF_PTR(Elf64_Phdr, phdr, i * ehdr.e_phentsize);
		if (p->p_type == PT_INTERP) {
			char* interp_path = (char*)(file_buffer + p->p_offset);

			auto interp_path_ = str(interp_path);
			serial2_printf("found interp %s\n",
			               interp_path_->c_str);
			if (resolve_dentry(interp_path_->c_str, 0,
			                   &interp_dentry, 0) != VFS_OK) {
				serial2_printf(
				    "error: dynamic linker not found..\n");
				str_release(interp_path_);
				return -1;
			}
			str_release(interp_path_);
			break;
		}
	}

	struct elf_load_mmap_table* mmap_table =
	    (struct elf_load_mmap_table*)kalloc(
	        sizeof(struct elf_load_mmap_table) * ehdr.e_phnum);

	auto l = elf_load(page, file_buffer, base_addr, base_addr, mmap_table);
	serial2_printf("loaded size %d kb\n", l / 1024);

	uintptr_t temporary_base = 0;
	for (int i = 0; i < ehdr.e_phnum; i++) {
		if (mmap_table[i].mapped) {
			temporary_base = mmap_table[i].vaddr;
			break;
		}
	}

	// find heap start
	uintptr_t heap_start = 0;
	{
		for (uint64_t i = 0; i < ehdr.e_phnum; i++) {
			Elf64_Phdr* p =
			    ELF_PTR(Elf64_Phdr, phdr, i * ehdr.e_phentsize);
			if (p->p_type == PT_LOAD) {
				auto end = base_addr + p->p_vaddr + p->p_memsz;
				if (end > heap_start)
					heap_start = end;
			}
		}
		heap_start = ALIGN_UP(heap_start, 0x1000);

		serial2_printf("heap start at 0x%x\n", heap_start);
	}

	serial2_printf("temporary_base 0x%x -> base 0x%x\n", temporary_base,
	               base_addr);
	if (!temporary_base) {
		serial2_printf("error temporary addr must be not empty\n");
		kfree2(mmap_table);
		dentry_put(loaded_file_dentry);
		return -2;
	}

	GnuHashHeader gnu_hash;
	Elf64_Shdr* gnu_hash_sym = sh_map.gnuhash;
	elf_gnu_hash_parse(&gnu_hash, gnu_hash_sym, file_buffer);

	Elf64_Dyn* dyn = elf_get_phdr_dynamic(file_buffer);
	if (dyn && !interp_dentry) {
		LOG2_INFO("VOXMO", "dynamic section found at 0x%x", dyn);
		elf_dynamic_map dyn_map = {0};
		elf_dyn_map_all(dyn, file_buffer, &dyn_map);
		LOG2_INFO("VOXMO", "strtab found at 0x%x", dyn_map.strtab);
		LOG2_INFO("VOXMO", "needed size %d", dyn_map.needed.size);
		elf_relocate_dyn(&dyn_map, temporary_base, base_addr, &gnu_hash,
		                 0, mmap_table, ehdr.e_phnum);
	}

	serial2_printf("base vaddr %x %x\n", base_vaddr,
	               (ehdr.e_entry - base_vaddr));

	auto entry_addr =
	    (ehdr.e_type == ET_EXEC) ? ehdr.e_entry : ehdr.e_entry + base_addr;

	// ld handle
	// TODO: will be refactor
	uintptr_t interp_base_addr = 0;
	uintptr_t interp_entry_addr = 0;
	Elf64_Ehdr interp_ehdr;

	auto user_thread_vm_page = create_vmm_page();

	struct elf_load_mmap_table* interp_mmap_table = 0;

	if (interp_dentry) {
		// handle interp
		auto interp_file_size = interp_dentry->vnode->size;
		serial2_printf("interp found %s size %d\n",
		               interp_dentry->name->c_str, interp_file_size);

		dentry_ptr interp_link_dentry = 0;

		if (interp_dentry->vnode->type == VNODE_TYPE_LNK) {
			char interp_link_path[255];
			auto interp_ops =
			    (vops_lnk_t*)interp_dentry->vnode->ops;
			interp_ops->readlink(interp_dentry->vnode,
			                     interp_link_path, 255);
			serial2_printf("link path %s\n", interp_link_path);

			if (resolve_dentry(interp_link_path, 0,
			                   &interp_link_dentry, 0) != VFS_OK) {
				serial2_printf("failed resolve %s\n",
				               interp_link_path);
				return -1;
			}
		}
		auto ld_so_size = interp_link_dentry->vnode->size;
		uint8_t* ld_so_buffer = (uint8_t*)kalloc(ld_so_size);
		if (!ld_so_buffer) {
			LOG2_ERROR("PROCESS", "unable to alloc %d kb",
			           ld_so_size / 1024);
			dentry_put(interp_dentry);
			return -1;
		}

		auto ld_so_ops = (vops_file_t*)interp_link_dentry->vnode->ops;
		if (!ld_so_ops) {
			LOG_ERROR("PROCESS", "ops is null");
			kfree2(ld_so_buffer);
			dentry_put(interp_dentry);
			return -1;
		}

		ld_so_ops->read(interp_link_dentry->vnode, ld_so_buffer,
		                ld_so_size, 0);
		serial2_printf("interp file size %d kb\n",
		               (uint32_t)ld_so_size / 1024);

		memcopy(&interp_ehdr, (void*)ld_so_buffer, sizeof(Elf64_Ehdr));

		size_t ld_so_size_4k =
		    ALIGN_UP(1 + ld_so_size, BLOCK_SIZE) / BLOCK_SIZE;

		interp_base_addr = vma_lookup_free_vaddr(
		    get_kernel_vmm_page(), VMA_REGION_PROCESS, ld_so_size_4k);
		LOG2_INFO("PROCESS", "interp %s has base addr at 0x%x",
		          interp_dentry->name->c_str, interp_base_addr);

		interp_mmap_table = (struct elf_load_mmap_table*)kalloc(
		    sizeof(struct elf_load_mmap_table) * interp_ehdr.e_phnum);

		auto ll = elf_load(page, ld_so_buffer, interp_base_addr,
		                   interp_base_addr, interp_mmap_table);
		serial2_printf("ld.so loaded size %d kb\n", ll / 1024);

		interp_entry_addr = interp_base_addr + interp_ehdr.e_entry;
	}

	// Stack setup
	// TODO: user_stack_vaddr its allocated using vmm, because its
	// possible in the one page
	//  has more than 1 mapping (ex: stack and heap), so we need to
	//  register vma for stack
	auto stack_phys = (uintptr_t)phys_base_alloc(USER_STACK_PAGES);

	auto user_stack_vaddr = USER_STACK_VADDR - USER_STACK_SIZE + 4096;
	auto stack_vaddr_kernel = vma_lookup_free_vaddr(
	    get_kernel_vmm_page(), VMA_REGION_A, USER_STACK_PAGES);
	vma_register(get_kernel_vmm_page(), stack_phys, stack_vaddr_kernel,
	             USER_STACK_PAGES * 4096);
	vxMultipleMmap(page, user_stack_vaddr,
	               stack_phys, USER_STACK_PAGES, 0b111);
	vxMultipleMmap(paging_get_highest_page_map(), stack_vaddr_kernel, stack_phys,
	               USER_STACK_PAGES, 0b111);
	// vma_register(

	uintptr_t phdr_vaddr = (ehdr.e_type == ET_EXEC)
	                           ? base_vaddr + ehdr.e_phoff
	                           : base_addr + ehdr.e_phoff;

	int argc = 0;
	if (argv) {
		while (argv[argc])
			argc++;
	}

	const char* fallback_argv[] = {path, NULL};
	if (argc == 0) {
		argv = (char* const*)fallback_argv;
		argc = 1;
	}

	uintptr_t user_rsp = elf_prepare_stack(
	    stack_vaddr_kernel + USER_STACK_SIZE, USER_STACK_VADDR + 4096, argc, argv,
	    envp, &ehdr, entry_addr, phdr_vaddr, interp_base_addr);

	auto entr = entry_addr;
	if (interp_base_addr)
		entr = interp_entry_addr;

	paging_unmap_page(paging_get_highest_page_map(), stack_vaddr_kernel);
	// vma_unregister(get_kernel_vmm_page(), stack_vaddr);

	auto thr = create_thread(page, entr, user_rsp, user_stack_vaddr, (uint16_t)-1,
	                         2, THREAD_USER);
	auto procc = create_process(loaded_file_dentry->name->c_str, thr);
	auto caller_thr = get_current_core_data()->active_thread;
	if (caller_thr && caller_thr->process) {
		procc->parent_pid = caller_thr->process->pid;
	}
	procc->heap_start = heap_start;
	procc->heap_end = procc->heap_start;
	procc->vm_lock.next_ticket = procc->vm_lock.now_serving = 0;
	procc->vm_page = user_thread_vm_page;

	// regiter stack vma
	vma_register(user_thread_vm_page, stack_phys, user_stack_vaddr, USER_STACK_PAGES * 4096);

	for (int i = 0; i < ehdr.e_phnum; i++) {
		auto mm = &mmap_table[i];
		if (mm->mapped) {
			serial2_printf(
			    "process: added to vma aligned base 0x%x\n",
			    base_addr + mm->alligned);
			vma_register(user_thread_vm_page, mm->paddr,
			             base_addr + mm->alligned, mm->size * 4096);
		}
	}

	if (interp_base_addr && interp_mmap_table) {
		serial2_printf("interp base addr 0x%x\n", interp_base_addr);
		for (int i = 0; i < interp_ehdr.e_phnum; i++) {
			auto mm = &interp_mmap_table[i];

			if (mm->mapped) {
				serial_printf("process: added interp to vma "
				              "aligned base 0x%x\n",
				              interp_base_addr + mm->alligned);

				vma_register(user_thread_vm_page, mm->paddr,
				             interp_base_addr + mm->alligned,
				             mm->size * 4096);
			}
		}
	}

	print_dentry_tree(get_root_dentry(), 0);

	serial2_printf("process id %d\n", procc->pid);
	serial2_printf(
	    "done setuping executable, now ready to sended to scheduler\n");

	attach_to_scheduler(thr);

	kfree2(mmap_table);
	if (interp_mmap_table)
		kfree2(interp_mmap_table);

	pid_t new_pid = procc->pid;
	dentry_put(loaded_file_dentry);
	return (int)new_pid;
}

pid_t alloc_pid(void) {
	for (size_t i = 0; i < MAX_PID_ALLOWED / 8; i++) {
		uint8_t byte = pid_bitmap[i];

		if (byte == 0xFF)
			continue;

		uint8_t free_bit =
		    (uint8_t)__builtin_ctz((unsigned)(~byte & 0xFF));

		pid_bitmap[i] |= (1u << free_bit);
		return (pid_t)(i * 8 + free_bit);
	}

	return INVALID_PID;
}

void free_pid(pid_t pid) {
	pid_t curr_byte = pid / 8;
	uint8_t curr_bit = pid % 8;
	pid_bitmap[curr_byte] &= ~(1 << curr_bit);
}

process_t* create_process(char* name, thread_t* main_thread) {
	auto p = (process_t*)vxSlabAlloc(process_cache);
	auto name_len = strlen(name);
	if (name_len > 64)
		name_len = 64;
	strncpy(p->name, name, name_len);
	p->name[name_len] = '\0';
	p->pid = alloc_pid();

	p->main_thread = main_thread;
	main_thread->process = p;
	p->exit_code = 0;
	p->exited = false;
	p->fdtable = alloc_fdtable();
	p->cache.next = &p->cache;
	p->cache.prev = &p->cache;

	auto bucket_idx = p->pid % 256;
	auto h = process_bucket[bucket_idx].first;
	auto n = &p->cache;
	n->next = h;
	n->prev = NULL;
	if (h)
		h->prev = n;
	process_bucket[bucket_idx].first = n;

	p->next = p->prev = p;
	if (!_process_list) {
		_process_list = p;
	} else {
		struct process* tail = _process_list->prev;
		tail->next = p;
		p->prev = tail;
		p->next = _process_list;
		_process_list->prev = p;
	}

	dentry_ptr current_proc = 0;
	resolve_dentry(itoa(p->pid, 10), process_dentry, &current_proc,
	               CREATE_MISSING_ENTRY);

	dentry_ptr current_proc_fd = 0;
	resolve_dentry("fd", current_proc, &current_proc_fd,
	               CREATE_MISSING_ENTRY);

	auto tty_dentry = get_tty_dentry(0);
	auto tty_vnode = tty_dentry->vnode;
	// fd0 -> stdin (placeholder)
	{
		dentry_ptr fd_dentry;
		auto next_fd = p->fdtable->next_fd++;
		resolve_dentry(itoa(next_fd, 10), current_proc_fd, &fd_dentry,
		               CREATE_MISSING_ENTRY);
		fd_dentry->vnode = tty_vnode;

		auto fd0 = alloc_fd();
		fd0->vnode = tty_vnode;
		fd0->ops = tty_vnode->ops;
		p->fdtable->fds[next_fd] = fd0;
	}
	// fd1 → stdout (/dev/tty0)
	{
		dentry_ptr fd_dentry;
		auto next_fd = p->fdtable->next_fd++;
		resolve_dentry(itoa(next_fd, 10), current_proc_fd, &fd_dentry,
		               CREATE_MISSING_ENTRY);
		fd_dentry->vnode = tty_vnode;

		auto fd1 = alloc_fd();
		fd1->vnode = tty_vnode;
		fd1->ops = tty_vnode->ops;
		p->fdtable->fds[next_fd] = fd1;
	}
	// fd2 → stderr (TODO)
	{
		dentry_ptr fd_dentry;
		auto next_fd = p->fdtable->next_fd++;
		resolve_dentry(itoa(next_fd, 10), current_proc_fd, &fd_dentry,
		               CREATE_MISSING_ENTRY);
		fd_dentry->vnode = tty_vnode;

		auto fd2 = alloc_fd();
		fd2->vnode = tty_vnode;
		fd2->ops = tty_vnode->ops;
		p->fdtable->fds[next_fd] = fd2;
	}
	return p;
}

process_t* find_process_by_pid(pid_t pid) {
	if (!_process_list)
		return nullptr;
	process_t* curr = _process_list;
	do {
		if (curr->pid == pid) {
			return curr;
		}
		curr = curr->next;
	} while (curr != _process_list);
	return nullptr;
}
