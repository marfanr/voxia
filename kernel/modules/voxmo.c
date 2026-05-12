#include "libk/executable/elf.h"
#include "libk/serial.h"
#include <string.h>
#include <type.h>
#include <vector.h>
#include "memory/kalloc.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "modules/kernel_reader.h"
#include "procc/workqueue.h"
#include "vfs/enum.h"
#include <str.h>
#include <modules/voxmo.h>
#include <vfs/dentry.h>
#include <vfs/vnode.h>

static kstring default_voxmo_path;
static voxmo_loaded_module_t_ptr voxmo_modules;
static spinlock_t voxmo_list_lock = {0};

static voxmo_loaded_module_t_ptr vxGetVoxmoModule(kstring name) {
	spin_acquire(&voxmo_list_lock);
	auto curr_module = voxmo_modules;
	while (curr_module != NULL) {
		if (stringcmp(curr_module->name, name)) {
			spin_release(&voxmo_list_lock);
			return curr_module;
		}
		curr_module = curr_module->next;
	}
	spin_release(&voxmo_list_lock);
	LOG2_ERROR("VOXMO", "module %s not found", name->c_str);
	return nullptr;
}

static void proccess_elf(voxmo_loaded_module_t_ptr module) {
	spin_acquire(&module->lock);
	if (module->loaded) {
		spin_release(&module->lock);
		LOG2_INFO("VOXMO", "module %s already loaded",
			  module->name->c_str);
		return;
	}

	uint8_t* data = (uint8_t*) module->main_data;
	size_t loaded_size = elf_count_load_size(data);
	LOG2_INFO("VOXMO", "loaded size %d (%d kb)", loaded_size,
		  loaded_size / 1024.0f);
	size_t size_4k = ALIGN_UP(1 + loaded_size, BLOCK_SIZE) / BLOCK_SIZE;
	uintptr_t base_addr =
		vma_lookup_free_vaddr(VMA_REGION_KMODULE, size_4k);
	LOG2_INFO("VOXMO", "base addr : 0x%x", base_addr);

	Elf64_Ehdr* ehdr = (Elf64_Ehdr*) data;
	switch (ehdr->e_type) {
	case 1:
		LOG2_DEBUG("VOXMO", "ELF Type: Relocatable (ET_REL)");
		break;
	case 2:
		LOG2_DEBUG("VOXMO", "ELF Type: Executable (ET_EXEC)");
		break;
	case 3:
		LOG2_DEBUG("VOXMO", "ELF Type: Shared Object / PIE (ET_DYN)");
		break;
	default:
		LOG2_DEBUG("VOXMO", "ELF Type: Unknown (0x%x)", ehdr->e_type);
		break;
	}

	if (ehdr->e_type != 3) {
		spin_release(&module->lock);
		LOG2_ERROR("VOXMO", "not shared library");
		return;
	}

	elf_section_map sh_map = {0};
	elf_section_map_all(data, &sh_map);

	elf_mmap_got(&sh_map, base_addr);

	elf_load(data, base_addr);

	symbols_ptr_vector_t voxmo_load_syms;
	vector_init(&voxmo_load_syms);
	vector_push_back(&voxmo_load_syms, kernel_get_symbols());
	LOG2_INFO("VOXMO", "voxmo load external symbol count %d",
		  voxmo_load_syms.size);
	for (size_t i = 0; i < voxmo_load_syms.size; i++) {
		LOG2_INFO("VOXMO",
			  "external symbol [%d] name %s, item count %d", i,
			  voxmo_load_syms.data[i]->name,
			  voxmo_load_syms.data[i]->items.size);
	}

	GnuHashHeader gnu_hash;
	Elf64_Shdr* gnu_hash_sym = sh_map.gnuhash;
	elf_gnu_hash_parse(&gnu_hash, gnu_hash_sym, data);

	Elf64_Dyn* dyn = elf_get_phdr_dynamic(data);
	if (dyn) {
		LOG2_INFO("VOXMO", "dynamic section found at 0x%x", dyn);
		elf_dynamic_map dyn_map = {0};
		elf_dyn_map_all(dyn, data, &dyn_map);

		LOG2_INFO("VOXMO", "strtab found at 0x%x", dyn_map.strtab);
		LOG2_INFO("VOXMO", "needed size %d", dyn_map.needed.size);

		elf_relocate_dyn(&dyn_map, base_addr, &gnu_hash,
				 &voxmo_load_syms);
	}

	elf_call_init_array(&sh_map, base_addr);

	uintptr_t load_addr =
		elf_find_symbol("load", &gnu_hash, base_addr, &sh_map, data);
	LOG2_INFO("VOXMO", "load : 0x%x", load_addr);

	if (load_addr) {
		vector(workqueue_ptr_t)* dependency_workqueue =
			(vector(workqueue_ptr_t)*) kalloc(
				sizeof(vector(workqueue_ptr_t)));
		vector_init(dependency_workqueue);

		for (size_t i = 0; i < module->dependency_count; i++) {
			voxmo_loaded_module_t_ptr dep_module =
				vxGetVoxmoModule(module->dependency[i]);

			if (!dep_module) {
				LOG2_ERROR("VOXMO",
					   "dependency module %s not found, "
					   "aborting load of %s",
					   module->dependency[i]->c_str,
					   module->name->c_str);
				// kfree(dependency_workqueue);
				spin_release(&module->lock);
				return;
			}

			if (!dep_module->loaded || !dep_module->queue) {
				LOG2_ERROR(
					"VOXMO",
					"dependency %s not ready (loaded=%d, "
					"queue=0x%x), aborting load of %s",
					dep_module->name->c_str,
					dep_module->loaded, dep_module->queue,
					module->name->c_str);
				// kfree(dependency_workqueue);
				spin_release(&module->lock);
				return;
			}

			vector_push_back(dependency_workqueue,
					 dep_module->queue);
			LOG2_INFO("VOXMO", "dependency %s added to queue 0x%x",
				  dep_module->name->c_str, dep_module->queue);
		}

		LOG2_INFO("VOXMO", "dependency workqueue size %d",
			  dependency_workqueue->size);

		{
			auto queue = vxAddWorkqueueTask(
				(void (*)(void*)) load_addr, nullptr,
				dependency_workqueue);
			module->queue = queue;
			module->loaded = true;
			LOG2_INFO("VOXMO", "module %s task created, queue=0x%x",
				  module->name->c_str, module->queue);
		}
	} else {
		LOG2_ERROR("VOXMO", "load not found");
	}
	module->loaded = true;
	spin_release(&module->lock);
}

void vxVoxmoInstall(const char* path) {

	kstring full_path = str_concat(default_voxmo_path, path);
	kstring full_path_with_ext = str_concat(full_path, ".voxmo");
	str_release(full_path);

	LOG2_INFO("VOXMO", "installing module from %s",
		  full_path_with_ext->c_str);

	dentry_ptr dentry = 0;
	if (vxResolveDentry(full_path_with_ext->c_str, 0, &dentry, 0)
	    != VFS_OK) {
		LOG2_ERROR("VOXMO", "module %s not found", path);
		str_release(full_path_with_ext);
		return;
	}

	size_t file_size = dentry->vnode->size;
	uint8_t* data = kalloc(file_size);
	LOG2_INFO("VOXMO", "allocated at 0x%x for size %d kb", data,
		  file_size / 1024);

	auto a = ((vops_file_t*) dentry->vnode->ops)
			 ->read(dentry->vnode, data, file_size, 0);
	if (a < 0) {
		LOG2_ERROR("VOXMO", "failed to read module %s", path);
		kfree(data, file_size); // FIX: free + return, jangan lanjut
		str_release(full_path_with_ext);
		return;
	}

	voxmo_loaded_module_t_ptr module = (voxmo_loaded_module_t_ptr) kalloc(
		sizeof(voxmo_loaded_module_t));
	memset(module, 0, sizeof(*module));
	module->next = nullptr;
	module->lock = (spinlock_t){0};

	struct voxmo_metadata_header* header =
		(struct voxmo_metadata_header*) data;
	char* module_name =
		(char*) ((uintptr_t) data + header->nama_module.pos);
	module->name = str(module_name);

	str_trim(module->name);
	module->path = full_path_with_ext;
	module->loaded = false;
	module->queue = nullptr;

	char* main_file = (char*) ((uintptr_t) data + header->main_file.pos);
	LOG2_INFO("VOXMO", "main file %s", main_file);

	uint16_t cap_count = header->capability.count;
	struct voxmo_metadata_string* cap_array =
		(struct voxmo_metadata_string*) ((uint64_t) data
						 + header->capability.pos);

	module->capability_count = cap_count;
	module->capability = (kstring*) kalloc(sizeof(kstring) * cap_count);

	for (uint16_t i = 0; i < cap_count; i++) {
		struct voxmo_metadata_string* cap = &cap_array[i];
		char* cap_name = (char*) ((uintptr_t) data + cap->pos);
		LOG2_INFO("VOXMO", "capability name %s", cap_name);
		module->capability[i] = str(cap_name);
	}

	module->dependency_count = header->dependency.count;
	module->dependency =
		(kstring*) kalloc(sizeof(kstring) * module->dependency_count);

	uint16_t dep_count = header->dependency.count;
	LOG2_INFO("VOXMO", "dependency count %d", dep_count);
	struct voxmo_metadata_string* dep_array =
		(struct voxmo_metadata_string*) ((uint64_t) data
						 + header->dependency.pos);
	for (uint16_t i = 0; i < dep_count; i++) {
		struct voxmo_metadata_string* dep = &dep_array[i];
		char* dep_name = (char*) ((uintptr_t) data + dep->pos);
		LOG2_INFO("VOXMO", "dependency name %s", dep_name);
		module->dependency[i] = str(dep_name);
	}

	boolean_t main_found = false;
	for (uint32_t i = 0; i < header->file_counts; i++) {
		struct voxmo_metadata_file* file =
			(struct
			 voxmo_metadata_file*) (data + header->header_len
						+ i
							  * sizeof(
								  struct
								  voxmo_metadata_file));

		char* file_name =
			(char*) ((uintptr_t) data + file->nama_file.pos);
		LOG2_INFO("VOXMO", "file name %s", file_name);
		LOG2_INFO("VOXMO", "main file found at offset 0x%x, size %d",
			  file->offset, file->size);

		if (strncmp(file_name, main_file, header->main_file.length)
		    == 0) {
			uint8_t* main_data = (uint8_t*) kalloc(file->size);
			LOG2_INFO("VOXMO", "main data buffer allocated at 0x%x",
				  main_data);
			memset(main_data, 0, file->size);
			memcopy((void*) main_data,
				(void*) (data + file->offset), file->size);
			module->main_data = (uintptr_t) main_data;

			main_found = true;
			break;
		}
	}

	if (!main_found) {
		LOG2_ERROR("VOXMO", "main file not found");
		/* Cleanup module yang sudah di-alloc sebelum return */
		str_release(module->name);
		str_release(module->path);
		kfree(module, sizeof(voxmo_loaded_module_t));
		kfree(data, file_size);
		return;
	}

	spin_acquire(&voxmo_list_lock);
	auto curr_module = voxmo_modules;
	if (!curr_module) {
		voxmo_modules = module;
	} else {
		while (curr_module->next != nullptr) {
			curr_module = curr_module->next;
		}
		curr_module->next = module;
	}
	spin_release(&voxmo_list_lock);

	LOG2_INFO("VOXMO", "module %s installed", module->name->c_str);
	kfree(data, file_size);
}

void vxSetDefaultVoxmoPath(const char* path) {
	default_voxmo_path = str(path);
}

void vxVoxmoProbe(kstring name) {
	LOG2_INFO("VOXMO", "probing module %s", name->c_str);
	voxmo_loaded_module_t_ptr module = vxGetVoxmoModule(name);
	if (!module) {
		LOG2_ERROR("VOXMO", "module %s not found", name->c_str);
		return;
	}

	if (module->loaded) {
		LOG2_INFO("VOXMO", "module %s already loaded", name->c_str);
		return;
	}

	if (module->dependency_count) {
		for (size_t i = 0; i < module->dependency_count; i++) {
			LOG2_INFO("VOXMO", "dependency %s",
				  module->dependency[i]->c_str);
			vxVoxmoProbe(module->dependency[i]);
		}
	}

	proccess_elf(module);
	LOG2_INFO("VOXMO", "module %s loaded", module->name->c_str);
}

void vxVoxmoReload() {
	spin_acquire(&voxmo_list_lock);
	voxmo_loaded_module_t_ptr m = voxmo_modules;
	spin_release(&voxmo_list_lock);

	while (m != nullptr) {
		if (!m->loaded) {
			LOG2_INFO("VOXMO", "load module %s", m->name->c_str);
			vxVoxmoProbe(m->name);
		}

		spin_acquire(&voxmo_list_lock);
		m = m->next; // FIX: selalu maju, regardless loaded atau tidak
		spin_release(&voxmo_list_lock);
	}
	LOG2_INFO("VOXMO", "all module reloaded");
}