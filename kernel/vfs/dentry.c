#include "vfs/dentry.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "libk/string.h"
#include "libk/type.h"
#include "libk/vector.h"
#include "memory/slab.h"
#include "vfs/enum.h"
#include "vfs/vnode.h"

static struct slab_cache* dentry_cache = 0;
static dentry_t* root_dentry = 0;

dentry_ptr vxCreateDentry(string name, vnode_t* vnode) {
	if (!dentry_cache)
		vxCreateSlabCache(&dentry_cache, "dentry", sizeof(dentry_t), 64,
		                  0);

	dentry_t* dentry = (dentry_t*)vxSlabAlloc(dentry_cache);
	memset(dentry, 0, sizeof(dentry_t));
	dentry->name = name;
	dentry->vnode = vnode;

	if (vnode)
		vector_push_back(&vnode->dentry_list, dentry);
	vector_init(&dentry->children);

	return dentry;
}

int vxNamei(char* path, dentry_ptr* out) {
	{
		auto curr_entry = root_dentry;
		vector(string) exploded_path = {0};
		vector_init(&exploded_path);
		explode(path, '/', &exploded_path);

		for (size_t i = 0; i < exploded_path.size; i++) {
			boolean_t found = false;
			for (size_t j = 0; j < curr_entry->children.size; j++) {
				dentry_t* child = curr_entry->children.data[j];

				if (stringcmp(child->name,
				              exploded_path.data[i])) {
					curr_entry = child;
					found = true;
					break;
				}
			}

			if (!found && i != exploded_path.size - 1) {
				for (size_t j = 0; j < exploded_path.size; j++)
					str_release(exploded_path.data[j]);
				vector_destroy(&exploded_path);
				return 0;
			} else if (!found) {
				auto new_entry =
				    vxCreateDentry(exploded_path.data[i], 0);
				vxAttachDentryToParent(new_entry, curr_entry);

				curr_entry = new_entry;
			}
		}

		*out = curr_entry;

		for (size_t j = 0; j < exploded_path.size - 1; j++)
			str_release(exploded_path.data[j]);
		vector_destroy(&exploded_path);
	}
	return VFS_OK;
}

void vxAttachDentryToParent(dentry_ptr dentry, dentry_ptr parent) {
	dentry->parent = parent;
	vector_push_back(&parent->children, dentry);
}

void vxAttachDentryToVnode(dentry_ptr dentry, vnode_t* vnode) {
	dentry->vnode = vnode;
	vector_push_back(&vnode->dentry_list, dentry);
}

void vxSetDentryAsRoot(dentry_ptr dentry) { root_dentry = dentry; }

dentry_ptr vxGetRootDirectory() { return root_dentry; }

int vxResolveDentry(char* path, dentry_ptr parent, dentry_ptr* out,
                    uint8_t flag) {
	auto curr_entry = root_dentry;
	if (parent)
		curr_entry = parent;

	{
		vector(string) exploded_path = {0};
		vector_init(&exploded_path);
		explode(path, '/', &exploded_path);

		for (size_t i = 0; i < exploded_path.size; i++) {
			boolean_t found = false;
			for (size_t j = 0; j < curr_entry->children.size; j++) {
				dentry_t* child = curr_entry->children.data[j];

				if (stringcmp(child->name,
				              exploded_path.data[i])) {
					curr_entry = child;
					found = true;
					break;
				}
			}

			if (!found) {
				if (flag & CREATE_MISSING_ENTRY) {
					auto new_netry = vxCreateDentry(
					    exploded_path.data[i], NULL);
					vxAttachDentryToParent(new_netry,
					                       curr_entry);
					curr_entry = new_netry;

					// LOG_INFO("DENTRY",
					//          "crated missing entry %s",
					//          new_netry->name->c_str);
					continue;
				}
				if (flag & RESOLVE_LAST_ENTRY) {
					*out = curr_entry;
					return i + 1;
				}
				for (size_t j = 0; j < exploded_path.size; j++)
					str_release(exploded_path.data[j]);
				vector_destroy(&exploded_path);
				return VFS_ENOENT;
			}
		}

		*out = curr_entry;
		if (flag & ~CREATE_MISSING_ENTRY) {
			for (size_t j = 0; j < exploded_path.size; j++)
				str_release(exploded_path.data[j]);
			vector_destroy(&exploded_path);
		}
		return VFS_OK;
	}
}
