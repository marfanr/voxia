#include "libk/executable/elf.h"
#include "libk/serial.h"
#include "libk/string.h"
#include "libk/type.h"
#include "libk/vector.h"
#include "memory/kalloc.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "memory/vm_manager.h"
#include "modules/kernel_reader.h"
#include "procc/workqueue.h"
#include "vfs/enum.h"
#include <libk/str.h>
#include <modules/voxmo.h>
#include <vfs/dentry.h>
#include <vfs/vnode.h>

typedef voxmo_loaded_module_t* voxmo_loaded_module_t_ptr;
define_vector(voxmo_loaded_module_t_ptr);
static vector(voxmo_loaded_module_t_ptr) voxmo_modules;
static string default_voxmo_path;

static voxmo_loaded_module_t_ptr vxGetVoxmoModule(string name) {
	for (size_t i = 0; i < voxmo_modules.size; i++) {
		LOG2_INFO("VOXMO", "checking module %s",
		          voxmo_modules.data[i]->name->c_str);
		if (stringcmp(voxmo_modules.data[i]->name, name)) {
			LOG2_INFO("VOXMO", "module %s found",
			          voxmo_modules.data[i]->name->c_str);
			return voxmo_modules.data[i];
		}
	}
	LOG2_ERROR("VOXMO", "module %s not found", name->c_str);
	return nullptr;
}

static void proccess_elf(voxmo_loaded_module_t_ptr module) {
	if (module->loaded) {
		LOG2_INFO("VOXMO", "module %s already loaded",
		          module->name->c_str);
		return;
	}
	uint8_t* data = (uint8_t*)module->main_data;
	size_t loaded_size = elf_count_load_size(data);
	LOG2_INFO("VOXMO", "loaded size %d (%f kb)", loaded_size,
	          loaded_size / 1024.0f);
	size_t size_4k = ALIGN_UP(1 + loaded_size, BLOCK_SIZE) / BLOCK_SIZE;
	uintptr_t base_addr =
	    vma_lookup_free_vaddr(VMA_REGION_KMODULE, size_4k);
	LOG2_INFO("VOXMO", "base addr : 0x%x", base_addr);

	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
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
		LOG2_ERROR("VOXMO", "not shared library");
		return;
	}

	// find symbol EhciModule::setup
	elf_section_map sh_map = {0};
	elf_section_map_all(data, &sh_map);

	// mappin got plt
	elf_mmap_got(&sh_map, base_addr);

	// load
	elf_load(data, base_addr);

	symbols_ptr_vector_t voxmo_load_syms;
	vector_init(&voxmo_load_syms);
	// load kernel symbol
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

		// TODO: handle unresolved symbol
		// kalau gagal reoslve tidak usa di muat kernel module nya
		elf_relocate_dyn(&dyn_map, base_addr, &gnu_hash,
		                 &voxmo_load_syms);
	}

	elf_call_init_array(&sh_map, base_addr);

	// call load
	uintptr_t load_addr =
	    elf_find_symbol("load", &gnu_hash, base_addr, &sh_map, data);
	LOG2_INFO("VOXMO", "load : 0x%x", load_addr);

	if (load_addr) {

		// if module has dependency, we need to load it after all
		// dependency loaded LOG2_INFO("VOXMO", "module %s has
		// dependency, will load after all dependency loaded",
		//          module->name->c_str);

		vector(workqueue_ptr_t)* dependency_workqueue = (vector(
		    workqueue_ptr_t)*)kalloc(sizeof(vector(workqueue_ptr_t)));
		vector_init(dependency_workqueue);
		for (size_t i = 0; i < module->dependency.size; i++) {
			voxmo_loaded_module_t_ptr dep_module =
			    vxGetVoxmoModule(module->dependency.data[i]);
			if (!dep_module) {
				LOG2_ERROR("VOXMO",
				           "dependency module %s not found",
				           module->dependency.data[i]->c_str);
				continue;
			}
			proccess_elf(dep_module);
			vector_push_back(dependency_workqueue,
			                 dep_module->queue);
			LOG2_INFO("VOXMO", "dependency %s added to queue 0x%x",
			          dep_module->name->c_str, dep_module->queue);
		}

		LOG2_INFO("VOXMO", "dependency workqueue size %d",
		          dependency_workqueue->size);

		{
			module->loaded = true;
			// auto dep       = module->dependency.size > 0 ?
			// &dependency_workqueue : 0;
			auto queue =
			    vxAddWorkqueueTask((void (*)(void*))load_addr,
			                       nullptr, dependency_workqueue);
			module->queue = queue;
			LOG2_INFO("VOXMO", "module %s task created",
			          module->name->c_str);
		}
	} else {
		LOG2_ERROR("VOXMO", "load not found");
	}
}

void vxVoxmoInstall(const char* path) {
	// TODO: handle multiple module with same name
	if (!voxmo_modules.size) {
		vector_init(&voxmo_modules);
	}

	string full_path = str_concat(default_voxmo_path, path);
	string full_path_with_ext = str_concat(full_path, ".voxmo");
	str_release(full_path);

	LOG2_INFO("VOXMO", "installing module from %s",
	          full_path_with_ext->c_str);

	dentry_ptr dentry = 0;
	if (vxResolveDentry(full_path_with_ext->c_str, 0, &dentry, 0) !=
	    VFS_OK) {
		LOG2_ERROR("VOXMO", "module %s not found", path);
		return;
	}

	str_release(full_path_with_ext);
	uint8_t* data = kalloc(dentry->vnode->size);
	((vops_file_t*)dentry->vnode->ops)
	    ->read(dentry->vnode, data, dentry->vnode->size, 0);

	voxmo_loaded_module_t_ptr module =
	    (voxmo_loaded_module_t_ptr)kalloc(sizeof(voxmo_loaded_module_t));
	vector_init(&module->capability);
	vector_init(&module->dependency);

	struct voxmo_metadata_header* header =
	    (struct voxmo_metadata_header*)data;
	char* module_name = (char*)((uintptr_t)data + header->nama_module.pos);
	module->name = str(module_name);
	str_trim(module->name);
	module->path = full_path_with_ext;
	module->loaded = false;

	char* main_file = (char*)((uintptr_t)data + header->main_file.pos);
	LOG2_INFO("VOXMO", "main file %s", main_file);

	uint16_t cap_count = header->capability.count;
	struct voxmo_metadata_string* cap_array =
	    (struct voxmo_metadata_string*)((uint64_t)data +
	                                    header->capability.pos);

	for (uint16_t i = 0; i < cap_count; i++) {
		struct voxmo_metadata_string* cap = &cap_array[i];
		char* cap_name = (char*)((uintptr_t)data + cap->pos);
		LOG2_INFO("VOXMO", "capability name %s", cap_name);
		vector_push_back(&module->capability, str(cap_name));
	}

	uint16_t dep_count = header->dependency.count;
	LOG2_INFO("VOXMO", "dependency count %d", dep_count);
	struct voxmo_metadata_string* dep_array =
	    (struct voxmo_metadata_string*)((uint64_t)data +
	                                    header->dependency.pos);
	for (uint16_t i = 0; i < dep_count; i++) {
		struct voxmo_metadata_string* dep = &dep_array[i];
		char* dep_name = (char*)((uintptr_t)data + dep->pos);
		LOG2_INFO("VOXMO", "dependency name %s", dep_name);
		vector_push_back(&module->dependency, str(dep_name));
	}

	// find main
	boolean_t main_found = false;
	for (uint32_t i = 0; i < header->file_counts; i++) {
		struct voxmo_metadata_file* file =
		    (struct
		     voxmo_metadata_file*)(data + header->header_len +
		                           i * sizeof(
		                                   struct voxmo_metadata_file));

		char* file_name =
		    (char*)((uintptr_t)data + file->nama_file.pos);
		LOG2_INFO("VOXMO", "file name %s", file_name);

		if (strncmp(file_name, main_file, header->main_file.length) ==
		    0) {
			uint8_t* main_data = (uint8_t*)kalloc(file->size);
			memcopy((void*)main_data, (void*)(data + file->offset),
			        file->size);
			module->main_data = (uintptr_t)main_data;
			main_found = true;
			break;
		}
	}

	if (!main_found) {
		LOG2_ERROR("VOXMO", "main file not found");
		return;
	}

	vector_push_back(&voxmo_modules, module);
	LOG2_INFO("VOXMO", "module %s installed , modules count %d",
	          module->name->c_str, voxmo_modules.size);
}

void vxSetDefaultVoxmoPath(const char* path) { default_voxmo_path = str(path); }

void vxVoxmoProbe(string name) {
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

	// check dependency
	if (module->dependency.size) {
		for (size_t i = 0; i < module->dependency.size; i++) {
			LOG2_INFO("VOXMO", "dependency %s",
			          module->dependency.data[i]->c_str);
			vxVoxmoProbe(module->dependency.data[i]);
		}
	}

	// load elf
	proccess_elf(module);
	LOG2_INFO("VOXMO", "module %s loaded", name->c_str);
}

void vxVoxmoReload() {
	for (size_t i = 0; i < voxmo_modules.size; i++) {
		if (!voxmo_modules.data[i]->loaded)
			vxVoxmoProbe(voxmo_modules.data[i]->name);
	}
	LOG2_INFO("VOXMO", "all module reloaded");
}

// TODO: will be removed, we didnt need dynamic linking in
// kernel module
// if (dyn_map.needed.size) { 	uintptr_t
// lib_base = 0x8000000; 	for (size_t i = 0; i <
// dyn_map.needed.size; i++) { 		char* lib_name =
// 		    (char*)(dyn_map.strtab +
// 		            dyn_map.needed.data[i]);
// 		LOG2_INFO("VOXMO", "required lib name : %s",
// 		          lib_name);

// 		// TODO: check if library is already loaded on
// 		// same pml4

// 		struct Library* lib = library_load(lib_name);
// 		// if (lib->is_loaded)
// 		// {
// 		//     continue;
// 		// }

// 		uint8_t* lib_data = (uint8_t*)lib->entry;

// 		Elf64_Ehdr* ehdr = (Elf64_Ehdr*)lib_data;
// 		switch (ehdr->e_type) {
// 		case 1:
// 			LOG2_DEBUG(
// 			    "VOXMO",
// 			    "ELF Type: Relocatable (ET_REL)");
// 			break;
// 		case 2:
// 			LOG2_DEBUG(
// 			    "VOXMO",
// 			    "ELF Type: Executable (ET_EXEC)");
// 			break;
// 		case 3:
// 			LOG2_DEBUG("VOXMO",
// 			          "ELF Type: Shared Object / "
// 			          "PIE (ET_DYN)");
// 			break;
// 		default:
// 			LOG2_DEBUG("VOXMO",
// 			          "ELF Type: Unknown (0x%x)",
// 			          ehdr->e_type);
// 			break;
// 		}
// 		LOG2_INFO("VOXMO", "lib data : 0x%x", lib_data);

// 		// elf
// 		elf_section_map lib_sh_map = {0};
// 		elf_section_map_all(lib_data, &lib_sh_map);

// 		// todo: dynamic addr for library base
// 		if (!lib->is_loaded) {
// 			elf_mmap_got(&lib_sh_map, lib_base);
// 			elf_load(lib_data, lib_base);
// 		}

// 		Elf64_Dyn* lib_dyn =
// 		    elf_get_phdr_dynamic(lib_data);

// 		// elf_map
// 		elf_dynamic_map lib_dyn_map = {0};
// 		elf_dyn_map_all(lib_dyn, lib_data,
// 		                &lib_dyn_map);

// 		LOG2_INFO("VOXMO", "strtab found at 0x%x",
// 		          lib_dyn_map.strtab);
// 		LOG2_INFO("VOXMO", "needed found at 0x%x",
// 		          lib_dyn_map.needed);

// 		GnuHashHeader lib_gnu_hash;
// 		Elf64_Shdr* lib_gnu_hash_sym =
// 		    lib_sh_map.gnuhash;
// 		elf_gnu_hash_parse(&lib_gnu_hash,
// 		                   lib_gnu_hash_sym, lib_data);

// 		// relocate
// 		if (!lib->is_loaded) {
// 			elf_relocate_dyn(&lib_dyn_map, lib_base,
// 			                 &lib_gnu_hash,
// 			                 &voxmo_load_syms);
// 			lib->is_loaded = true;
// 			elf_call_init_array(&lib_sh_map,
// 			                    lib_base);
// 		}

// 		// load symbol
// 		elf_get_symbol(lib_name, lib_base, &lib_sh_map,
// 		               lib_data, &voxmo_load_syms,
// 		               true);

// 		// call init construct
// 	}
// }