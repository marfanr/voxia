#include "libk/serial.h"
#include "vfs/enum.h"
#include <str.h>
#include <vfs/filesystem.h>

static filesystem_t* registered_filesystems = 0;

int create_filesystem(char name[16], struct fs_data* fs_data) {
	if (!fs_data->ops) {
		return VFS_ERR;
	}
	
	serial2_printf("[DEBUG] create_filesystem: fs_data->ops = %x, sizeof(fs_data) = %d\n", fs_data->ops, sizeof(struct fs_data));

	auto fs = (struct filesystem*)kalloc(sizeof(struct filesystem));
	memset(fs, 0, sizeof(struct filesystem));
	strncpy(fs->name, name, 16);

	memcopy(&fs->data, fs_data, sizeof(struct fs_data));
	
	serial2_printf("[DEBUG] create_filesystem: AFTER memcopy, fs->data.ops = %x\n", fs->data.ops);


	auto curr = &registered_filesystems;
	while (*curr)
		curr = &(*curr)->next;
	*curr = fs;

	return VFS_OK;
}

KERNEL_API filesystem_t* retrieve_filesystem(const char name[16]) {
	serial2_printf("[DEBUG] registered_filesystems is stored at %x, value is %x\n", &registered_filesystems, registered_filesystems);
	filesystem_t* curr = registered_filesystems;
	while (curr != 0) {
		serial2_printf("[DEBUG] Checking fs at %x: name='%s', next=%x\n", curr, curr->name, curr->next);
		if (strncmp(curr->name, name, strlen(name)) == 0) {
			serial2_printf("[DEBUG] retrieve_filesystem: found %s at %x, ops=%x\n", name, curr, curr->data.ops);
			return curr;
		}
		curr = curr->next;
	}
	serial2_printf("[DEBUG] retrieve_filesystem: NOT FOUND\n");
	return 0;
}