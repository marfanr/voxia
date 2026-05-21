#include "./library.h"
#include "vfs/dentry.h"
#include "vfs/vnode.h"
#include <hal/cpu/paging.h>
#include <libk/executable/elf.h>
#include <libk/serial.h>
#include <str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <vfs/vfs.h>

static struct Library* libraries = NULL;

void library_register(const char* path, enum LibraryType type) {
	// TODO: buka file hanya ketika di load saja
	dentry_ptr opened_dentry = 0;
	resolve_dentry((char*) path, 0, &opened_dentry, 0);
	if (!opened_dentry) {
		LOG_ERROR("LIBRARY", "failed to open file %s", path);
		return;
	}

	uint8_t* file_data = (uint8_t*) (kalloc(opened_dentry->vnode->size));
	memset(file_data, 0, opened_dentry->vnode->size);
	((vops_file_t*) opened_dentry->vnode->ops)
		->read(opened_dentry->vnode, file_data,
		       opened_dentry->vnode->size, 0);

	struct Library* lib = (kalloc(sizeof(struct Library)));
	memset((void*) lib, 0, sizeof(struct Library));

	lib->name = opened_dentry->name->c_str;
	lib->type = type;
	lib->entry = (uintptr_t) file_data;

	if (libraries == 0) {
		libraries = lib;
	} else {
		struct Library* current = libraries;
		while (current->next != 0) {
			current = current->next;
		}
		current->next = lib;
	}

	LOG_INFO("LIBRARY", "success registerd %s", lib->name);
}

struct Library* library_load(const char* name) {
	struct Library* current = libraries;
	while (current != 0) {
		if (strncmp(current->name, name, strlen(name)) == 0) {
			LOG_INFO("LIBRARY", "success load %s", current->name);
			return current;
		}
		current = current->next;
	}
	return 0;
}

// TODO: tambahkan unload