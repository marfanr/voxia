#ifndef __VFS__DENTRY_H__
#define __VFS__DENTRY_H__

#include "llist.h"
#include "vfs/cache.h"
#include "vfs/rcu.h"
#include <string.h>
#include <type.h>
#include <vector.h>

#define DENTRY_PINNED (1 << 0)
#define DENTRY_NEGATIVE (1 << 1)
#define DENTRY_MOUNTPOINT (1 << 2)
#define DENTRY_IN_CACHE (1 << 3)  /* dentry ada di vfs_cache, diset saat insert */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dentry dentry_t;
typedef struct dentry* dentry_ptr;

struct vnode;

struct hlist_node;
struct hlist_head;
struct dentry {
	struct llist_head child_list;
	struct llist_head siblings __attribute__((aligned(64)));
	atomic_t refcount;
	uint32_t hash;
	uint32_t flags;     /* DENTRY_PINNED, DENTRY_IN_CACHE, dst */
	kstring name;
	struct vnode* vnode;
	dentry_ptr parent;
	struct hlist_node hash_node;
	struct rcu_head rcu;
} __attribute__((aligned(64)));

int vxnamei(const char* path, dentry_ptr* out);

dentry_ptr KERNEL_API create_dentry(kstring name, struct vnode* vnode,
                                    dentry_ptr parent);

void vxSetDentryAsRoot(dentry_ptr dentry);
dentry_ptr get_root_dentry();

enum {
	RESOLVE_LAST_ENTRY = (1 << 1),
	CREATE_MISSING_ENTRY = (1 << 2),
};

int resolve_dentry(const char* path, dentry_ptr parent, dentry_ptr* out,
                   uint8_t flag);

void vxFreeDentry(dentry_ptr dentry);
void vxFreeDentryWithChildren(dentry_ptr dentry);

typedef struct vnode* vnode_ptr_t;
void vxAttachDentryToVnode(dentry_ptr dentry, vnode_ptr_t vnode);

uint32_t hash_dentry(const char* name, dentry_ptr parent);

void dentry_put(dentry_ptr dentry);
void dentry_get(dentry_ptr dentry);
void delete_dentry(dentry_t* node);

int get_reffcount(dentry_ptr dentry);

void print_dentry_tree(dentry_t* node, int depth);

kstring get_full_path_from_dentry(dentry_ptr dentry);

#ifdef __cplusplus
}
#endif

#endif // __VFS__DENTRY_H__