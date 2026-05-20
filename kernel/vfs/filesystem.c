#include <vfs/filesystem.h>
#include "vfs/enum.h"
#include <str.h>

static filesystem_t* registered_filesystems = 0;

int create_filesystem( char name[16], struct fs_data* fs_data) {
    auto fs = (struct filesystem*)kalloc(sizeof(struct filesystem));
    memset(fs, 0, sizeof(struct filesystem));
    strncpy(fs->name, name, 16);

    memcopy(&fs->data, fs_data, sizeof(struct fs_data));

    auto curr = &registered_filesystems;
    while (*curr)
        curr = &(*curr)->next;
    *curr = fs;
    
	return VFS_OK;
}

KERNEL_API filesystem_t* retrieve_filesystem(const char name[16]) {
	filesystem_t* curr = registered_filesystems;
	while (curr != 0) {
		if (strncmp(curr->name, name, strlen(name)) == 0)
			return curr;
		curr = curr->next;
	}
	return 0;
}