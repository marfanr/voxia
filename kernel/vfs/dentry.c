#include <vfs/dentry.h>
#include <str.h>
#include <string.h>
#include <vector.h>
#include <type.h>
#include <hash.h>
#include <llist.h>

#include <spinlock.h>
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
	auto h = hash32(name, 0);
	if (parent) {
		uintptr_t p = (uintptr_t) parent;
		h ^= (uint32_t) (p & 0xFFFFFFFF);
		h ^= (uint32_t) (p >> 32);
	}
	return h;
}

static spinlock_t lock = {0};

dentry_ptr KERNEL_API create_dentry(kstring name, vnode_t* vnode,
				    dentry_ptr parent) {
	if (!dentry_cache)
		vxCreateSlabCache(&dentry_cache, "dentry",
				  sizeof(struct dentry), 0, 0);

	spin_acquire(&lock);
	// dentry_t* dentry = (dentry_t*) vxSlabAlloc(dentry_cache);
	dentry_t* dentry = (dentry_t*) kalloc(sizeof(struct dentry));
	memset(dentry, 0, sizeof(dentry_t));

	__atomic_fetch_add(&dentry->refcount.counter, 1, __ATOMIC_RELAXED);
	dentry->hash = hash_dentry(name->c_str, parent);
	dentry->name = name;
	dentry->vnode = vnode;
	dentry->parent = parent;

	dentry->hash_node.next = dentry->hash_node.prev = &dentry->hash_node;
	dentry->hash_node.dentry = (void*) dentry;

	llist_init(&dentry->siblings);
	llist_init(&dentry->child_list);
	spin_release(&lock);

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

// TODO: check path its not comming from BSS
#define PATH_MAX 4096

//

int KERNEL_API vxnamei2(const char* path, dentry_ptr* out) {
	if (!path || !out)
		return -1;
	*out = NULL;

	size_t len = strlen(path);
	if (len == 0 || len >= PATH_MAX)
		return -1;

	char* temp = (char*) kalloc(len + 1);
	// serial2_printf("temp alloc: %p to %p\n", temp, temp + len + 1);

	if (!temp)
		return -1;

	memset(temp, 0, len + 1);
	memcopy(temp, (void*) path, len);
	char* path_iter = temp;

	auto root_cache = get_root_cache();
	dentry_t* curr = root_dentry;
	if (!curr) {
		kfree(temp, len + 1);
		return -1;
	}

	dentry_get(curr);

	while (path_iter && *path_iter != '\0') {
		char* component = strsep2(&path_iter, "/");
		if (!component || component[0] == '\0')
			continue;

		dentry_t* next = cache_lookup(root_cache, curr, component);

		if (!next) {
			auto name_copy = str(component);

			dentry_t* new_entry = create_dentry(name_copy, 0, curr);
			// ↑ str_from_owned: buat kstring dari pointer yang sudah di-alloc
			//   sehingga lifetime-nya tidak tergantung temp

			if (!new_entry) {
				// kfree(name_copy, name_copy->len + 1);
				str_release(name_copy);
				dentry_put(curr);
				kfree(temp, len + 1);
				return -1;
			}

			// vfs_cache_insert(root_cache, new_entry);
			dentry_get(new_entry);
			dentry_put(curr);
			curr = new_entry;
			continue;
		}

		dentry_put(curr);
		curr = next;
	}

	// ✓ Aman: kfree temp SETELAH semua component sudah di-copy
	// serial2_printf("temp free: %p\n", temp);
	kfree(temp, len + 1);

	*out = curr;
	return VFS_OK;
}

int KERNEL_API vxnamei(const char* path, dentry_ptr* out) {
	if (!path || !out)
		return -1;

	*out = NULL;

	// pastikan ada terminator dalam PATH_MAX
	size_t len = strlen(path);

	if (len == 0 || len >= PATH_MAX) {
		return -1;
	}

	// jangan stack terlalu besar kalau kernel stack kecil
	char* temp = (char*) kalloc(len + 1);

	if (!temp) {
		return -1;
	}

	memset(temp, 0, len + 1);
	memcopy(temp, (void*) path, len);

	auto root_cache = get_root_cache();

	dentry_t* curr = root_dentry;

	if (!curr) {
		kfree2(temp);
		return -1;
	}

	dentry_get(curr);

	serial2_printf("root dentry: %s\n",
		       curr->name ? curr->name->c_str : "NULL");

	char* path_iter = temp;

	serial2_printf("[DBG] temp=0x%p len=%d\n", temp, len);
	while (path_iter && *path_iter != '\0') {
		char* component = strsep2(&path_iter, "/");

		if (!component || component[0] == '\0')
			continue;

		// optional safety
		// if (strlen(component) >= 128) {
		// 	dentry_put(curr);
		// 	// kfree(temp);
		// 	return -1;
		// }

		dentry_t* next = cache_lookup(root_cache, curr, component);

		if (!next) {
			serial2_printf("created new entry : %s\n", component);

			dentry_t* new_entry =
				create_dentry(str(component), 0, curr);

			if (!new_entry) {
				dentry_put(curr);
				kfree2(temp);
				return -1;
			}

			vfs_cache_insert(root_cache, new_entry);

			// dentry_get(new_entry);

			// dentry_put(curr);

			curr = new_entry;
			continue;
		}

		// dentry_put(curr);

		curr = next;
	}

	// kfree(temp, len + 1);

	*out = curr;

	return VFS_OK;
}

void delete_dentry(dentry_t* node) {
	if (!node)
		return;

	// hitung jumlah children dulu
	size_t n = 0;
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
	for (size_t i = 0; i < n; i++)
		delete_dentry(children[i]);

	if (children)
		kfree(children, sizeof(dentry_t*) * n);
}
