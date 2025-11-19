#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/string.h"
#include "memory/kalloc.h"
#include "memory/phys_base_allocator.h"
#include "vfs/cache.h"
#include "vfs/file.h"
#include <libk/type.h>
#include <vfs/vfs.h>

#include <hal/block/block.h>
#include <libk/debug/debug.h>
#include <libk/hash.h>
#include <libk/serial.h>
#include <libk/str.h>

#include <libk/vector.h>
#include <memory/slab.h>
#include <memory/vm_manager.h>
#include <sys/descriptor.h>
#include <vfs/dentry.h>
#include <vfs/filesystem.h>

#define RBT_TYPE vfs_inode_t
#define RBT_ID_NAME id
#include <libk/tree/rbt.h>

// definition
#define VFS_DEBUG 1
#define PATH_MAX 4096
#define NAME_MAX 255
#define VFS_MAX_FS 512
#define VFS_MAX_PATH_CACHE 512
#define CALC_PATH_HASH_LEN(path, curr_depth) 4096 + 1024 * pow(strlen(path), curr_depth)

uint64_t vfs_last_existing_inode = 0;

// internal use
static rbt_node          *NIL;
static rbt_node          *vfs_tree;
static struct slab_cache *rbt_node_cache;
static struct slab_cache *vfs_inode_cache;
static dentry_ptr         root_dentry;

INIT(Vfs)
{
    slab_cache_create(&rbt_node_cache, "rbt_node", sizeof(rbt_node), 64, 0);
    slab_cache_create(&vfs_inode_cache, "vfs_inode", sizeof(struct vfs_inode), 64, 0);

    NIL           = (struct rbt_node *)slab_alloc(rbt_node_cache);
    NIL->data     = (vfs_inode_t *)slab_alloc(vfs_inode_cache);
    NIL->data->id = 0;
    NIL->left = NIL->right = NIL->parent = NIL;
    vfs_tree                             = NIL;

    // create root inode
    struct vfs_inode *root_inode = vfs_create_inode(0);
    root_dentry                  = create_directory_entry(str("/"), root_inode, 0);

    root_inode->is_directory = 1;

    LOG_INFO("vfs", "vfs has been installed");
}

struct vfs_inode *
vfs_create_inode(filesystem_t *fs)
{
    struct vfs_inode *inode = (struct vfs_inode *)slab_alloc(vfs_inode_cache);
    inode->fs               = fs;
    inode->id               = vfs_last_existing_inode++;
    inode->is_mounted       = false;
    inode->is_directory     = false;
    inode->permission       = 0;
    inode->ref_count        = 0;
    inode->size             = 0;
    inode->offset           = 0;
    vector_init(&inode->dentry_list);

    rbt_node *node = (rbt_node *)slab_alloc(rbt_node_cache);
    rbt_insert_node(&vfs_tree, node, inode, NIL);

    return inode;
}

int
find_entry_path(const char *path)
{
    return 0;
}

void
strcat(char *dest, const char *src)
{
    while (*dest)
    {
        dest++;
    }
    while (*src)
    {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = 0;
}

KERNEL_API
vfs_mount_error
vfs_mount(const char *path, const char *block, const char *fs)
{
    // pertama lakukan cek terhadap block
    block_device *device = block_get_device(block);
    if (device == 0)
        return VFS_ERR_MOUNT_INVALID_BLOCK;

    if (device->used)
        return VFS_ERR_MOUNT_ALREADY_MOUNTED;

    filesystem_t *fs_ = filesystem_find(fs);
    if (fs_ == 0)
        return VFS_ERR_MOUNT_INVALID_FS;

    vector(string) exploded_path = {0};
    vector_init(&exploded_path);
    explode(path, '/', &exploded_path);

    struct vfs_inode *curr_inode;
    dentry_ptr        curr_entry = root_dentry;

    for (size_t i = 0; i < exploded_path.size; i++)
    {
        curr_inode = vfs_create_inode(fs_);
        curr_entry = create_directory_entry(exploded_path.data[i], curr_inode, curr_entry);
        curr_inode->is_directory = 1;
    }
    curr_entry->inode->is_mounted = true;
    curr_entry->inode->block      = device;

    if (device->ops->open(FMODE_R) > 0)
        return VFS_ERR_MOUNT_INVALID_BLOCK;

    if (fs_->ops->mount)
    {
        fs_->ops->mount(curr_entry->inode, curr_entry);
    }

    // make sure all ops must be implemented
    // if (device->ops->read == 0 || device->ops->write == 0)
    // {
    //     return VFS_ERR_MOUNT_INVALID_BLOCK;
    // }

    vector_destroy(&exploded_path);
    return 1;
}

int
vfs_umount(const char *path)
{
    // to be implemented
    return -1;
}

static dentry_t *
vxFindInode(const char *path)
{
    // exploding path
    vector(string) exploded_path = {0};
    vector_init(&exploded_path);

    explode(path, '/', &exploded_path);

    // todo: CACHE dirctory
    // TODO: implement mekanisme cache path biar ga usah explode terus
    // first find root block device
    dentry_t *curr_entry         = root_dentry;
    dentry_t *last_mounted_entry = root_dentry;
    size_t    mounted_depth      = 0;

    for (size_t i = 0; i < exploded_path.size; i++)
    {
        boolean_t found = false;

        if (curr_entry->children.size == 0)
        {
            break;
        }

        if (curr_entry->inode->is_mounted)
        {
            LOG_INFO("VFS", "found mounted at %s", curr_entry->name->c_str);
            last_mounted_entry = curr_entry;
            mounted_depth      = i - 1;
        }

        for (size_t j = 0; j < curr_entry->children.size; j++)
        {
            dentry_t *child = curr_entry->children.data[j];

            if (stringcmp(child->name, exploded_path.data[i]))
            {
                curr_entry = child;
                found      = true;
                break;
            }
        }

        if (!found)
        {
            LOG_ERROR("VFS", "path %s not found", path);
            vector_destroy(&exploded_path);
            return nullptr;
        }
    }

    // get child entry
    dentry_t *first_entry = 0;
    for (size_t i = 0; i < last_mounted_entry->children.size; i++)
    {
        dentry_t *child = last_mounted_entry->children.data[i];

        if (stringcmp(child->name, exploded_path.data[mounted_depth + 1]))
        {
            first_entry = child;
            break;
        }
    }

    if (first_entry == 0)
    {
        LOG_INFO("VFS", "first time mounted");
    }

    curr_entry = last_mounted_entry;

    for (size_t i = mounted_depth + 1; i < exploded_path.size; i++)
    {
        boolean_t found = false;
        for (size_t j = 0; j < curr_entry->children.size; j++)
        {
            dentry_t *child = curr_entry->children.data[j];

            if (stringcmp(child->name, exploded_path.data[i]))
            {
                curr_entry = child;
                found      = true;
            }
        }

        if (!found)
            goto fail;
    }

    vector_destroy(&exploded_path);
    return curr_entry;

fail:
    vector_destroy(&exploded_path);
    return nullptr;
}

// TODO: implement flag
//  not used now on initrd
file_t *
vxFileInternalOpen(const char *path, uint8_t flags)
{
    auto curr_entry = vxFindInode(path);
    if (curr_entry == 0)
    {
        LOG_WARN("VFS", "internal open file %s not found", path);
        return nullptr;
    }

    LOG_INFO("VFS", "file %s opened successfully", path);
    file_t *file       = (file_t *)kalloc(sizeof(file_t));
    file->ops          = curr_entry->inode->file_ops;
    file->private_data = (void *)curr_entry;
    return file;
}

int
vxVFSOpen(const char *path, vfs_open_mode flags)
{
    vfs_inode_t *cache_inode = vxCacheLookup(str(path));
    if (cache_inode)
    {
        // return descriptor_add(cache_inode, 0, flags);
    }

    auto inode = vxFindInode(path);
    if (inode == 0)
    {
        LOG_WARN("VFS", "internal open file %s not found", path);
        return nullptr;
    }

    // vxCacheNode(str(path), curr_entry->inode);
    LOG_INFO("VFS", "file %s opened successfully", path);
    // return descriptor_add(curr_entry->inode, curr_entry, flags);

    return -1;
}

KERNEL_API
int
vxVFSFileStat(file_t *file, vfs_file_stats_ptr buf)
{
    dentry_ptr curr_entry = (dentry_ptr)file->private_data;
    buf->size             = curr_entry->inode->size;
    buf->name             = curr_entry->name;

    return 0;
}

KERNEL_API
int
vxVFSRead(file_t *file, void *buf, size_t count)
{
    if (buf == 0)
    {
        return -2;
    }
    if (!file->ops)
        return -3;

    return file->ops->read(file, buf, count);

    return 0;
}

int
vxVFSClose(int fd)
{
    // struct file_descriptor *descriptor = descriptor_get(fd);
    // if (descriptor == 0)
    //     return -1;

    // descriptor_free(fd);
    return 0;
}

boolean_t
vxVFSIsDirectory(const char *path)
{
    // exploding path
    vector(string) exploded_path = {0};
    vector_init(&exploded_path);
    explode(path, '/', &exploded_path);

    dentry_t *curr_entry = root_dentry;

    for (size_t i = 0; i < exploded_path.size; i++)
    {
        boolean_t found = false;

        for (size_t j = 0; j < curr_entry->children.size; j++)
        {
            dentry_t *child = curr_entry->children.data[j];

            if (stringcmp(child->name, exploded_path.data[i]))
            {
                curr_entry = child;
                found      = true;
                break;
            }
        }

        if (!found)
        {
            vector_destroy(&exploded_path);
            return false;
        }
    }

    vector_destroy(&exploded_path);
    return curr_entry->inode->is_directory;
}

#undef RBT_ID_NAME
#undef RBT_TYPE
