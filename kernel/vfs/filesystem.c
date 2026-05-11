#include <vfs/filesystem.h>
#include "vfs/enum.h"
#include <str.h>

static filesystem_t* registered_filesystems = 0;

int vxCreateFilesystem(const char name[16], filesystem_t* fs) {
	strcpy((char*) fs->name, name);
	fs->next = 0;
	if (registered_filesystems == 0) {
		registered_filesystems = fs;
		return VFS_OK;
	}

	fs->next = registered_filesystems;
	registered_filesystems = fs;
	return VFS_OK;
}

filesystem_t* vxFindFilesystem(const char name[16]) {
	filesystem_t* curr = registered_filesystems;
	while (curr != 0) {
		if (strncmp(curr->name, name, strlen(name)) == 0)
			return curr;
		curr = curr->next;
	}
	return 0;
}