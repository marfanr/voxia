#include <vfs/dentry.h>
#include <str.h>
#include <string.h>
#include <vector.h>
#include <type.h>
#include <hash.h>
#include <llist.h>

#include "libk/serial.h"
#include "libk/type.h"
#include "llist.h"
#include "memory/kalloc.h"
#include "memory/slab.h"
#include "vfs/cache.h"
#include "vfs/enum.h"
#include "vfs/rcu.h"
#include "vfs/vnode.h"

static struct slab_cache* dentry_cache = 0;
static dentry_t* root_dentry = 0;

inline uint32_t hash_dentry(const char* name, dentry_ptr parent) {
	auto h = hash(name, 0);
	if (parent) {
		uintptr_t p = (uintptr_t) parent;
		// h ^= (uint32_t) (p & 0xFFFFFFFF);
		// h ^= (uint32_t) (p >> 32);
	}
	return h;
}

dentry_ptr KERNEL_API create_dentry(kstring name, vnode_t* vnode,
				    dentry_ptr parent) {
	if (!dentry_cache)
		vxCreateSlabCache(&dentry_cache, "dentry", sizeof(dentry_t), 64,
				  0);

	dentry_t* dentry = (dentry_t*) vxSlabAlloc(dentry_cache);
	memset(dentry, 0, sizeof(dentry_t));

	__atomic_fetch_add(&dentry->refcount.counter, 1, __ATOMIC_RELAXED);
	dentry->hash = hash_dentry(name->c_str, parent);
	dentry->name = name;
	dentry->vnode = vnode;
	dentry->parent = parent;

	llist_init(&dentry->siblings);
	llist_init(&dentry->child_list);
	return dentry;
}

static void dentry_free_rcu(struct rcu_head* head) {
	auto dentry = container_of(head, struct dentry, rcu);
	slab_free(dentry_cache, dentry);
}

void dentry_get(dentry_ptr dentry) {
	__atomic_fetch_add(&dentry->refcount.counter, 1, __ATOMIC_RELAXED);
}

void dentry_put(dentry_ptr dentry) {
	if (__atomic_fetch_sub(&dentry->refcount.counter, 1, __ATOMIC_RELAXED)
	    == 1) {
		call_rcu(&dentry->rcu, dentry_free_rcu);
	}
}

void KERNEL_API vxAttachDentryToVnode(dentry_ptr dentry, vnode_t* vnode) {
	dentry->vnode = vnode;
	// vector_push_back(&vnode->dentry_list, dentry);
}

void vxSetDentryAsRoot(dentry_ptr dentry) {
	root_dentry = dentry;
}

dentry_ptr KERNEL_API vxGetRootDirectory() {
	return root_dentry;
}

int KERNEL_API vxResolveDentry(char* path, dentry_ptr parent, dentry_ptr* out,
			       uint8_t flag) {
	if (!path || !out)
		return -1;

	auto root_cache = get_root_cache();

	dentry_t* curr = parent ? parent : root_dentry;
	dentry_get(curr);

	char* path_ = path;
	while (path_ != NULL && *path_ != '\0') {
		char* component = strsep2(&path_, "/");

		if (!component || strlen(component) == 0)
			continue;

		// strip trailing slash dan whitespace
		size_t len = strlen(component);
		while (len > 0
		       && (component[len - 1] == '/'
			   || component[len - 1] == ' '))
			component[--len] = '\0';

		if (len == 0)
			continue;

		component[len] = '\0'; // pastikan null-terminated

		if (strcmp(component, ".") == 0)
			continue;

		if (strcmp(component, "..") == 0) {
			if (curr->parent) {
				dentry_t* up = curr->parent;
				dentry_get(up);
				dentry_put(curr);
				curr = up;
			}
			continue;
		}

		dentry_t* next = cache_lookup(root_cache, curr, component);
		// LOG_DEBUG("VFS",
		// 	  "resolving '%s': current '%s', next 0x%lx (%s)",
		// 	  component, curr->name->c_str, next,
		// 	  next ? next->name->c_str : "NULL");

		if (!next) {
			if (flag & CREATE_MISSING_ENTRY) {
				// double check — mungkin sudah ada dengan nama bersih
				// next = cache_lookup(root_cache, curr,
				// 		    component);
				// if (next) {
				// 	dentry_put(curr);
				// 	curr = next;
				// 	continue;
				// }

				dentry_t* new_entry =
					create_dentry(str(component), 0, curr);
				vfs_cache_insert(root_cache, new_entry);
				dentry_get(new_entry);
				dentry_put(curr);
				curr = new_entry;
				continue;
			} else {
				dentry_put(curr);
				*out = NULL;
				return -1;
			}
		}

		dentry_put(curr);
		curr = next;
	}

	*out = curr;
	return VFS_OK;
}

int KERNEL_API vxNamei(char* path, dentry_ptr* out) {
	if (!path || !out)
		return -1;

	auto root_cache = get_root_cache();

	dentry_t* curr = root_dentry;
	dentry_get(curr);

	serial2_printf("root dentry: %s\n", curr ? curr->name->c_str : "NULL");

	char* path_ = path;
	while (path_ != NULL && *path_ != '\0') {
		char* component = strsep2(&path_, "/");

		if (!component || strlen(component) == 0)
			continue;

		// lookup dulu, curr masih valid
		dentry_t* next = cache_lookup(root_cache, curr, component);

		if (!next) {
			// buat dengan curr masih valid sebagai parent
			serial2_printf("created new entry : %s\n", component);
			dentry_t* new_entry =
				create_dentry(str(component), 0, curr);
			vfs_cache_insert(root_cache, new_entry);
			dentry_get(new_entry);

			dentry_put(curr); // baru put
			curr = new_entry;
			continue;
		}

		dentry_put(curr);
		curr = next;
	}

	*out = curr;
	return VFS_OK;
}

void delete_dentry(dentry_t* node) {
	if (!node)
		return;

	// hitung jumlah children dulu
	int n = 0;
	struct llist_head* pos = node->child_list.next;
	while (pos != &node->child_list) {
		n++;
		pos = pos->next;
	}

	// alokasi dynamic
	dentry_t** children = NULL;
	if (n > 0) {
		children = (dentry_t**) kalloc(sizeof(dentry_t*) * n);
		int i = 0;
		pos = node->child_list.next;
		while (pos != &node->child_list) {
			children[i++] = container_of(pos, dentry_t, siblings);
			pos = pos->next;
		}
	}

	// hapus node ini
	cache_remove(get_root_cache(), node);
	dentry_put(node);

	// rekursi ke children
	for (int i = 0; i < n; i++)
		delete_dentry(children[i]);

	if (children)
		kfree(children, sizeof(dentry_t*) * n);
}
