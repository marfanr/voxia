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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dentry dentry_t;
typedef struct dentry* dentry_ptr;

struct vnode;

struct hlist_node;
struct hlist_head;
struct dentry {
	struct llist_head child_list; /* list anak-anak direktori ini */
	struct llist_head siblings __attribute__((aligned(64)));   /* posisi kita di child_list parent */
	atomic_t refcount;
	uint32_t hash;
	uint32_t flags;
	kstring name;
	struct vnode* vnode;
	dentry_ptr parent;
	struct hlist_node hash_node;  /* masuk dcache hash table */
	struct rcu_head rcu;
} __attribute__((aligned(64)));

/**
 * @brief Resolves a file path to a directory entry (dentry).
 *
 * This function traverses the filesystem tree starting from the root dentry.
 * It tokenizes the input path and iterates through the directory structure.
 *
 * Behavior:
 * - If an intermediate directory in the path is missing, the function fails.
 * - If the final component of the path is missing, a new dentry is created.
 *
 * @param path The null-terminated string representing the absolute path to
 * resolve.
 * @param out  A pointer to a dentry pointer. On success, this will be updated
 * to point to the resolved (or newly created) dentry.
 *
 * @return int Returns VFS_OK on success, or 0 if an intermediate directory
 * was not found.
 */
int vxnamei(const char* path, dentry_ptr* out);


/**
 * This function handles memory allocation for a new dentry using the slab
 * allocator. It also links the dentry to its corresponding VNode (if provided).
 *
 * @note This function performs lazy initialization of the dentry slab cache
 * if it does not exist yet.
 *
 * @param name  The name of the file or directory.
 * @param vnode Pointer to the associated VNode (inode data).
 * Pass NULL if the VNode is not yet available.
 *
 * @return dentry_ptr A pointer to the newly allocated and initialized dentry.
 */
dentry_ptr KERNEL_API create_dentry(kstring name, struct vnode* vnode,
				    dentry_ptr parent);

void vxSetDentryAsRoot(dentry_ptr dentry);
dentry_ptr get_root_dentry();

enum {
	RESOLVE_LAST_ENTRY = (1 << 1),
	CREATE_MISSING_ENTRY = (1 << 2),
};

/**
 * @brief Resolves a path to a directory entry (dentry) with configurable start
 * point and strictness.
 *
 * This function traverses the filesystem path. It differs from vxNamei by
 * allowing a specific parent dentry to start from, and it can handle cases
 * where the final component of the path does not exist yet (controlled by
 * flags).
 *
 * @param path   The file path to resolve (can be relative or absolute).
 * @param parent The dentry to start the search from. If NULL, starts from
 * `root_dentry`.
 * @param out    Double pointer to store the resulting dentry.
 * @param flag   Bitmask to modify behavior.
 * - If `RESOLVE_LAST_ENTRY` is set: If a path component is missing,
 * the function stops, sets `*out` to the *current* directory (the parent),
 * and returns the index of the missing component. This is useful when
 * creating new files/directories.
 *
 * @return int
 * - VFS_OK: The full path was successfully resolved to an existing dentry.
 * - VFS_ENOENT: A directory in the path was missing (and no flag was set).
 * - [Index]: If `RESOLVE_LAST_ENTRY` was set and the target was missing,
 * returns the index of the missing component in the path.
 */
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

void
print_dentry_tree(dentry_t* node, int depth);

kstring get_full_path_from_dentry(dentry_ptr dentry);

#ifdef __cplusplus
}
#endif

#endif // __VFS__DENTRY_H__