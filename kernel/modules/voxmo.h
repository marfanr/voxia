#ifndef __MODULES__VOXMO_H__
#define __MODULES__VOXMO_H__

#include <string.h>
#include <vector.h>
#include <type.h>
#include "procc/workqueue.h"

#pragma pack(push, 1)
struct voxmo_metadata_string {
	uint16_t length;
	uint64_t pos;
};

struct voxmo_metadata_list {
	uint16_t count;
	uint64_t pos;
};

struct voxmo_metadata_header {
	uint32_t magic;
	uint16_t version;
	uint32_t header_len;
	uint32_t file_counts;

	struct voxmo_metadata_string nama_module;
	struct voxmo_metadata_string description;
	struct voxmo_metadata_string license;
	struct voxmo_metadata_string version_str;
	struct voxmo_metadata_string author;
	struct voxmo_metadata_string main_file;

	struct voxmo_metadata_list capability;
	struct voxmo_metadata_list dependency;
};

struct voxmo_metadata_file {
	uint64_t offset;
	uint32_t metadata_length;
	uint32_t size;
	struct voxmo_metadata_string nama_file;
};
#pragma pack(pop)

typedef struct voxmo_loaded_module {
	kstring name;
	kstring* capability;
	kstring* dependency;
	size_t capability_count;
	size_t dependency_count;
	uintptr_t main_data;
	kstring path;
	boolean_t loaded;
	workqueue_t* queue;

	struct voxmo_loaded_module* next;
} voxmo_loaded_module_t __attribute__((aligned(64)));
typedef voxmo_loaded_module_t* voxmo_loaded_module_t_ptr;

void vxVoxmoInstall(const char* path);
void vxSetDefaultVoxmoPath(const char* path);
void vxVoxmoReload();

#endif // __MODULES__VOXMO_H__