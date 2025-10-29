#include "libk/string.h"
#include "libk/vector.h"
#include <libk/str.h>
#include <memory/slab.h>
#include <vfs/dentry.h>
#include <vfs/vfs.h>

static struct slab_cache *dentry_cache = 0;
static dentry_t          *root_dentry  = 0;

dentry_ptr
create_directory_entry(string name, struct vfs_inode *inode, dentry_t *parent)
{
    if (!dentry_cache)
        slab_cache_create(&dentry_cache, "dentry", sizeof(dentry_t), 64, 0);

    dentry_t *dentry = (dentry_t *)slab_alloc(dentry_cache);
    memset(dentry, 0, sizeof(dentry_t));
    dentry->name   = name;
    dentry->inode  = inode;
    dentry->parent = parent;
    if (inode)
        vector_push_back(&inode->dentry_list, dentry);
    vector_init(&dentry->children);

    // if root not exist
    if (root_dentry == 0 && parent == NULL)
    {
        root_dentry = dentry;
        return dentry;
    }
    else
    {
        if (parent == NULL)
            return dentry;

        vector_push_back(&parent->children, dentry);
    }
    return dentry;
}