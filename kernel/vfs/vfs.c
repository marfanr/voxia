#include <vfs/vfs.h>

#include <hal/block/block.h>
#include <libk/debug/debug.h>
#include <libk/hash.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <sys/descriptor.h>

#define VFS_DEBUG 0

static vfs_t *g__vfs_ = 0;

#define VFS_MAX_FS 512
#define VFS_MAX_PATH_CACHE 512
#define CALC_PATH_HASH_LEN(path, curr_depth) \
    4096 + 1024 * pow(strlen(path), curr_depth)

struct vfs_rb_node *NIL;
uint64_t            vfs_last_existing_inode = 0;

void vfs_install() {
    NIL               = (struct vfs_rb_node *)(phys_base_alloc(
        1 + sizeof(struct vfs_rb_node) / 4096));
    NIL->color        = RBT_BLACK;
    NIL->inode        = (vfs_inode_t *)(phys_base_alloc(1 + sizeof(vfs_inode_t) / 4096));
    NIL->inode->id    = 0;
    NIL->inode->entry = (struct vfs_entry *)(phys_base_alloc(
        1 + sizeof(struct vfs_entry) / 4096));
    NIL->left = NIL->right = NIL->parent = NIL;

    g__vfs_             = (vfs_t *)(phys_base_alloc(1 + sizeof(vfs_t) / 4096));
    g__vfs_->inode_root = NIL;
    g__vfs_->entry_root = 0;
    g__vfs_->mount_list = 0;
    g__vfs_->fs_list    = (struct vfs_fs **)(phys_base_alloc(
        1 + sizeof(struct vfs_fs *) * VFS_MAX_FS / 4096));
    g__vfs_->path_cache = (struct vfs_path_cache **)(phys_base_alloc(
        1 + sizeof(struct vfs_path_cache *) * VFS_MAX_PATH_CACHE / 4096));
    // KDEBUG (DEBUG_LEVEL_INFO, "[OK] VFS installed");
}

void strcpy(char *dest, const char *src) {
    while (*src) {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = 0;
}

void rbt_rotate_left(struct vfs_rb_node **root, struct vfs_rb_node *x) {
    struct vfs_rb_node *y = x->right;
    x->right              = y->left;
    if (y->left != NIL)
        y->left->parent = x;

    y->parent = x->parent;
    if (x->parent == 0)
        *root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left   = x;
    x->parent = y;
}

void rbt_rotate_right(struct vfs_rb_node **root, struct vfs_rb_node *y) {
    struct vfs_rb_node *x = y->left;
    y->left               = x->right;
    if (x->right != NIL) {
        x->right->parent = y;
    }
    x->parent = y->parent;
    if (y->parent == 0) {
        *root = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    x->right  = y;
    y->parent = x;
}

void rbt_fix_insert(struct vfs_rb_node **root, struct vfs_rb_node *y) {
    while (y->parent->color == RBT_RED) {
        if (y->parent == y->parent->parent->left) {
            struct vfs_rb_node *x = y->parent->parent->right;
            if (x != 0 && x->color == RBT_RED) {
                y->parent->color         = RBT_BLACK;
                x->color                 = RBT_BLACK;
                y->parent->parent->color = RBT_RED;
                y                        = y->parent->parent;
            } else {
                if (y == y->parent->right) {
                    y = y->parent;
                    rbt_rotate_left(root, y);
                }
                y->parent->color         = RBT_BLACK;
                y->parent->parent->color = RBT_RED;
                rbt_rotate_right(root, y->parent->parent);
            }
        } else {
            struct vfs_rb_node *x = y->parent->parent->left;
            if (x != 0 && x->color == RBT_RED) {
                y->parent->color         = RBT_BLACK;
                x->color                 = RBT_BLACK;
                y->parent->parent->color = RBT_RED;
                y                        = y->parent->parent;
            } else {
                if (y == y->parent->left) {
                    y = y->parent;
                    rbt_rotate_right(root, y);
                }
                y->parent->color         = RBT_BLACK;
                y->parent->parent->color = RBT_RED;
                rbt_rotate_left(root, y->parent->parent);
            }
        }
    }
    (*root)->color = RBT_BLACK;
}

int rbt_insert_node(struct vfs_rb_node **root, vfs_inode_t *inode) {
    struct vfs_rb_node *z = (struct vfs_rb_node *)(phys_base_alloc(
        1 + sizeof(struct vfs_rb_node) / 4096));
    memset(z, 0, sizeof(struct vfs_rb_node));
    z->inode = inode;
    z->left = z->right = NIL;
    z->color           = RBT_RED;

    // mencari posisi
    struct vfs_rb_node *parent = 0;
    struct vfs_rb_node *curent = *root;
    while (curent != NIL) {
        // serial_trace ("}} curent->inode->id : %d\n", curent->inode->id);
        parent = curent;

        if (z->inode->id < curent->inode->id)
            curent = curent->left;
        else
            curent = curent->right;
    }

    z->parent = parent;
    if (parent == 0) {
        // serial_trace ("}} root is null\n");
        *root = z;
    } else if (z->inode->id < parent->inode->id)
        parent->left = z;
    else
        parent->right = z;

    if (z->parent == 0) {
        z->color = RBT_BLACK;
        return 1;
    }

    if (z->parent->parent == 0) {
        return 1;
    }

    rbt_fix_insert(root, z);
    return 1;
}

struct vfs_rb_node *
rbt_find_node(struct vfs_rb_node *root, int id) {
    if (root == NIL || root->inode->id == id)
        return root;

    if (id < root->inode->id)
        return rbt_find_node(root->left, id);

    return rbt_find_node(root->right, id);
}

void rbt_debug(struct vfs_rb_node *root) {
    if (root != NIL) {
        rbt_debug(root->left);

        serial_trace("[DEBUG] inode id : %d, name : %s size %d\n", root->inode->id, root->inode->entry->name, root->inode->entry->inode->size);
        rbt_debug(root->right);
    }
}

vfs_inode_t *
get_inode_by_id(int id) {
    struct vfs_rb_node *rb_node = rbt_find_node(g__vfs_->inode_root, id);
    if (rb_node == 0)
        return 0;
    return rb_node->inode;
}

void vfs_register_fs(const char *name, vfs_operations_t *ops, boolean_t has_own_inode) {
    struct vfs_fs *fs       = (struct vfs_fs *)(phys_base_alloc(
        1 + sizeof(struct vfs_fs) / 4096));
    fs->name                = name;
    fs->ops                 = ops;
    fs->has_own_inode       = has_own_inode;
    int index               = hash(name, VFS_MAX_FS);
    g__vfs_->fs_list[index] = fs;
}

struct vfs_fs *
vfs_get_fs(const char *name) {
    int index = hash(name, VFS_MAX_FS);
    serial_trace("get fs : %s\n", name);
    return g__vfs_->fs_list[index];
}

char **
explode_path(const char *path) {
    // serial_trace ("exploding path : %s\n", path);
    char **result = (char **)(phys_base_alloc(
        1 + sizeof(char *) * strlen(path) / 4096));
    memset(result, 0, sizeof(char *) * strlen(path));

    int   i = 0, j = 0;
    char *buffer = (char *)(phys_base_alloc(1 + 256 / 4096));
    memset(buffer, 0, 1 + 256);
    while (*path) {
        if (*path == '/') {
            buffer[i] = 0;
            result[j] = (char *)(phys_base_alloc(
                1 + sizeof(buffer) / 4096));
            memset(result[j], 0, sizeof(buffer));
            // if buffer is empty add /
            if (strlen(buffer) == 0)
                memcopy(result[j], "/", sizeof(char));
            else
                memcopy(result[j], buffer, sizeof(buffer));
            // serial_trace ("result[%d] : %s\n", j, result[j]);
            i = 0;
            memset(buffer, 0, 256);
            j++;
        } else {
            buffer[i] = *path;
            i++;
        }
        path++;
    }
    if (i > 0) {
        buffer[i] = 0;
        result[j] = (char *)(phys_base_alloc(1 + strlen(buffer) / 4096));
        strcpy(result[j], buffer);
        // serial_trace ("result[%d] : %s\n", j, result[j]);
        j++;
    }
    phys_base_free((void *)(uint64_t)buffer, 1 + 256 / 4096);
    return result;
}

int calc_path_depth(const char *path) {
    int depth = 0;
    while (*path) {
        if (*path == '/') {
            depth++;
        }
        path++;
    }
    return depth + 1;
}

#define ALLOC_INODE \
    (vfs_inode_t *)(phys_base_alloc(1 + sizeof(vfs_inode_t) / 4096))
#define ALLOC_ENTRY                       \
    (struct vfs_entry *)(phys_base_alloc( \
        1 + sizeof(struct vfs_entry) / 4096))

struct vfs_entry *
vfs_create_entry(const char *name, vfs_inode_t *inode,
                 struct vfs_entry *parent) {
    struct vfs_entry *entry = ALLOC_ENTRY;
    memset(entry, 0, sizeof(struct vfs_entry));
    entry->name   = name;
    entry->inode  = inode;
    entry->parent = parent;
    return entry;
}

vfs_inode_t *
vfs_create_inode(uint64_t id, struct vfs_fs *fs) {
    vfs_inode_t *inode = ALLOC_INODE;
    inode->fs          = fs;
    inode->id          = id;
    rbt_insert_node(&g__vfs_->inode_root, inode);
    // rbt_debug (g__vfs_->inode_root);
    return inode;
}

int find_entry_path(const char *path) {
}

void strcat(char *dest, const char *src) {
    while (*dest) {
        dest++;
    }
    while (*src) {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = 0;
}

uint64_t
pow(uint64_t base, uint64_t exp) {
    uint64_t result = 1;
    for (uint64_t i = 0; i < exp; i++) {
        result *= base;
    }
    return result;
}

void save_mount(vfs_inode_t *node, struct vfs_fs *fs, struct block_device *block) {
    struct vfs_mount *mount = (struct vfs_mount *)(phys_base_alloc(
        1 + sizeof(struct vfs_mount) / 4096));
    mount->inode            = node;
    mount->fs               = fs;
    mount->block            = block;
    mount->next             = 0;
    struct vfs_mount *curr  = g__vfs_->mount_list;
    if (curr == 0) {
        g__vfs_->mount_list = mount;
        return;
    }
    while (curr->next != 0)
        curr = curr->next;
    curr->next = mount;
}

void vfs_caching_path(const char *path, vfs_inode_t *inode,
                      struct vfs_entry *entry) {
    int                         index      = hash(path, VFS_MAX_PATH_CACHE);
    struct vfs_path_cache_item *cache_item = (struct vfs_path_cache_item *)(phys_base_alloc(
        1 + sizeof(struct vfs_path_cache_item) / 4096));
    cache_item->path                       = path;
    cache_item->inode                      = inode;
    cache_item->entry                      = entry;
    cache_item->next                       = 0;
    cache_item->used_count                 = 0;

    struct vfs_path_cache *cache = g__vfs_->path_cache[index];
    if (cache == 0)
        cache = (struct vfs_path_cache *)(phys_base_alloc(
            1 + sizeof(struct vfs_path_cache) / 4096));

    if (cache->cache != 0) {
        struct vfs_path_cache_item *curr = cache->cache;
        while (curr->next != 0)
            curr = curr->next;
        curr->next = cache_item;
    } else
        cache->cache = cache_item;
    g__vfs_->path_cache[index] = cache;
}

struct vfs_path_cache_item *
vfs_get_cached_path(const char *path) {
    int                    index = hash(path, VFS_MAX_PATH_CACHE);
    struct vfs_path_cache *cache = g__vfs_->path_cache[index];
    if (cache == 0)
        return 0;
    struct vfs_path_cache_item *curr = cache->cache;
    while (strncmp(curr->path, path, strlen(path)) != 0)
        curr = curr->next;
    curr->used_count++;
    return cache;
}

void add_new_entry(struct vfs_entry *entry, struct vfs_entry *parent) {
    serial_trace("add new entry : %s ,parent ; 0x%x\n", entry->name, parent);
    if (parent->child_count == 0) {
        serial_trace("parent->child_count == 0\n");
        parent->child = (struct vfs_entry **)(phys_base_alloc(
            1 + VFS_ENTRY_PRE_ALLOC * sizeof(struct vfs_entry *) / 4096));

    } else if (parent->child_count % VFS_ENTRY_PRE_ALLOC == 0) {
        struct vfs_entry **temp = (struct vfs_entry **)(phys_base_alloc(
            1 + (parent->child_count + VFS_ENTRY_PRE_ALLOC) * sizeof(struct vfs_entry *) / 4096));
        serial_trace("temp at 0x%x, parent child %d\n", temp, parent->child_count);
        memcopy(temp, parent->child,
                parent->child_count * sizeof(struct vfs_entry *));
        serial_trace("created new entry\n");
        phys_base_free((void *)
            (uint64_t)parent->child,
            1 + parent->child_count * sizeof(struct vfs_entry *) / 4096);
        parent->child = temp;
    }
    serial_trace("parent->child_count : %d\n", parent->child_count);
    parent->child[parent->child_count] = entry;
    parent->child_count++;
}

/**
 * vfs_mount - Mount a filesystem to the specified path
 * @path: Target mount point in the filesystem hierarchy
 * @block: Block device path or identifier to mount
 * @fs: Filesystem type to use for mounting
 *
 * Mounts the specified filesystem type from the given block device
 * to the path location in the virtual filesystem hierarchy.
 *
 * Return: 0 on success, negative error code on failure
 */
int vfs_mount(const char *path, const char *block, const char *fs) {
    // pertama lakukan cek terhadap block
    struct block_device *device = block_get_device(block);
    if (device == 0)
        return VFS_MOUNT_NOT_FOUND;

    // serial_trace ("device : 0x%x\n", device);

    if (device->used)
        return VFS_MOUNT_ALREADY_MOUNTED;

    // lalu cek terhadap filesystem
    struct vfs_fs *_fs = vfs_get_fs(fs);
    if (_fs == 0)
        return VFS_MOUNT_INVALID_FS;

    int    path_depth    = calc_path_depth(path);
    char **exploded_path = explode_path(path);

    char *_path = (char *)(phys_base_alloc(1 + 256 / 4096));
    memset(_path, 0, 4096);
    vfs_inode_t      *curr_inode;
    struct vfs_entry *curr_entry = g__vfs_->entry_root;

    for (int i = 0; i < path_depth; i++) {
        strcat(_path, exploded_path[i]);
        // serial_trace ("path : %s\n", exploded_path[i]);
        uint64_t _hash = hash(_path, CALC_PATH_HASH_LEN(_path, i));
        if (i > 0 && i < path_depth - 1)
            strcat(_path, "/");

        serial_trace("current entry 0x%x\n", curr_entry);

        curr_inode = get_inode_by_id(_hash);

        if (curr_inode == 0 || curr_inode->id == 0) {
            curr_inode = vfs_create_inode(vfs_last_existing_inode++, _fs);

            struct vfs_entry *entry = vfs_create_entry(
                exploded_path[i], curr_inode, curr_entry);
            curr_inode->entry        = entry;
            curr_inode->is_directory = 1;
            curr_inode->block        = device;

            serial_trace("entry name : %s\n", entry->name);

            if (curr_entry != 0)
                add_new_entry(entry, curr_entry);

            if (strncmp(exploded_path[i], "/", 1) == 0) {
                g__vfs_->entry_root = entry;
            }
        } else {
            phys_base_free((void *)(void *)(uint64_t)exploded_path[i],
                           1 + strlen(exploded_path[i]) / 4096);
        }

        curr_entry = curr_inode->entry;
        serial_trace("mount path : %s  hash %d\n", _path, _hash);
    }

    curr_inode->is_mounted = 1;
    // serial_trace ("last node id : %d\n", curr_inode->id);
    save_mount(curr_inode, _fs, device);
    _fs->ops->mount(curr_inode);

    return 1;
}

int vfs_umount(const char *path) {
    // to be implemented
    return -1;
}

char *
rtrim(char *str) {
    int i = strlen(str) - 1;
    while (i >= 0 && str[i] == ' ') {
        str[i] = 0;
        i--;
    }
    return str;
}

struct vfs_mount *
vfs_get_mount(vfs_inode_t *inode) {
    struct vfs_mount *curr = g__vfs_->mount_list;
    while (curr != 0) {
        if (curr->inode == inode)
            return curr;
        curr = curr->next;
    }
    return 0;
}

int vfs_lookup(const char *path, int mount_inode) {
    int                 path_depth      = calc_path_depth(path);
    char              **exploded_path   = explode_path(path);
    struct vfs_rb_node *_mount_rb_inode = rbt_find_node(g__vfs_->inode_root, mount_inode);

    rbt_debug(g__vfs_->inode_root);
    if (_mount_rb_inode == 0) {
        return VFS_ERR_NOT_FOUND;
    }

    vfs_inode_t *_mount_inode = _mount_rb_inode->inode;

    serial_trace("mount inode : %s\n", _mount_inode->entry->name);

    struct vfs_mount *mount = vfs_get_mount(_mount_inode);

    int   mount_start = 0;
    char *_path       = (char *)(phys_base_alloc(1 + strlen(path) / 4096));

    char *_full_path = (char *)(phys_base_alloc(1 + strlen(path) / 4096));

    memset(_path, 0, 4096);
    memset(_full_path, 0, 4096);
    vfs_inode_t      *curr_inode = _mount_inode;
    struct vfs_entry *curr_entry = _mount_inode->entry;

    for (int i = 0; i < path_depth; i++) {
        strcat(_full_path, exploded_path[i]);

        // dipakai jika filesystem tidak memiliki sistem id sendiri
        int _hash = hash(_full_path, CALC_PATH_HASH_LEN(_full_path, i));
        if (i > 0 && i < path_depth - 1)
            strcat(_full_path, "/");

        if (strncmp(exploded_path[i], _mount_inode->entry->name,
                    strlen(exploded_path[i])) == 0)
            mount_start = i;

        if (i > 0 && i > mount_start && mount_start > 0) {
            strcat(_path, exploded_path[i]);
            if (i > 0 && i < path_depth - 1)
                strcat(_path, "/");

            struct vfs_entry *path_entry = ALLOC_ENTRY;
            path_entry->name             = exploded_path[i];
            vfs_inode_t *found_inode;
            _mount_inode->fs->ops->lookup(_mount_inode, path_entry, &found_inode);

            // harusnya ini memanggil lookup bukan open, dan bukan full path
            struct vfs_open_response *open_response = _mount_inode->fs->ops->open(mount->block->ops,
                                                                                  _path, curr_inode->id);

            int id = _hash;
            if (mount->fs->has_own_inode)
                id = open_response->id;

            curr_inode = get_inode_by_id(id);

            if (curr_inode == 0 || curr_inode->id == 0) {
                curr_inode              = vfs_create_inode(_hash, _mount_inode->fs);
                struct vfs_entry *entry = vfs_create_entry(
                    exploded_path[i], curr_inode, curr_entry);
                curr_inode->id           = id;
                entry->parent            = curr_entry;
                curr_inode->entry        = entry;
                curr_inode->is_directory = open_response->is_directory;
                curr_inode->size         = open_response->size;
                curr_inode->entry->addr  = open_response->addr;
                curr_inode->block        = mount->block;

                add_new_entry(entry, curr_entry);
            } else {
                // if not used freeing the path name
                phys_base_free((void *)
                    (void *)(uint64_t)exploded_path[i],
                    1 + strlen(exploded_path[i]) / 4096);
            }

            curr_entry = curr_inode->entry;
        }
    }

#if VFS_DEBUG
    serial_trace("[VFS] lookup end\n");
    rbt_debug(g__vfs_->inode_root);
#endif
    return curr_inode->id;
}

int get_curr_mount_inode_id_by_path(const char *path) {
    int    path_depth    = calc_path_depth(path);
    char **exploded_path = explode_path(path);

    char *_path = (char *)(phys_base_alloc(1));
    memset(_path, 0, 4096);

    int          hash_path;
    boolean_t         mount_found   = 0;
    vfs_inode_t *current_inode = g__vfs_->inode_root;

    for (int i = 0; i < path_depth; i++) {
        serial_trace("path : %s\n", exploded_path[i]);
        if (!mount_found) {

            // hash_path = hash(_path, CALC_PATH_HASH_LEN(_path, i));
            // if (i > 0 && i < path_depth - 1)
            //     strcat(_path, "/");

            struct vfs_mount *curr_mount = g__vfs_->mount_list;
            if (curr_mount != 0) {
                while (curr_mount != 0) {
                    serial_trace("mount entry name %s\n, curr mount id %d\n",
                                 curr_mount->inode->entry->name, curr_mount->inode->id);
                    int    mount_path_depth    = calc_path_depth(curr_mount->inode->entry->name);
                    char **mount_exploded_path = explode_path(curr_mount->inode->entry->name);

                    for (int j = 0; j < mount_path_depth; j++) {
                        serial_trace("mount path : %s\n", mount_exploded_path[j]);
                        if (strncmp(mount_exploded_path[j], exploded_path[i], strlen(exploded_path[i])) == 0) {
                            serial_trace("found mount path : %s\n", curr_mount->inode->entry->name);
                            hash_path   = curr_mount->inode->id;
                            mount_found = 1;
                            break;
                        }
                    }
                    // if (curr_mount->inode->id == hash_path) {
                    //     mount_found = 1;
                    // }
                    curr_mount = curr_mount->next;
                }
            }
        }
        phys_base_free((void *)(void *)(uint64_t)exploded_path[i],
                       1 + strlen(exploded_path[i]) / 4096);
    }
    phys_base_free((void *)(void *)(uint64_t)_path, 1);
    if (!mount_found)
        return -1;
    return hash_path;
}

// TODO: on migration on new vfs scheme
int vfs_open(const char *path, uint8_t flags) {
    serial_trace("vfs open : %s\n", path);
    int inode_id = get_curr_mount_inode_id_by_path(path);
    serial_trace("root inode id : %d\n", inode_id);

    int file = vfs_lookup(path, inode_id);
    serial_trace("lookup done\n");
    vfs_inode_t *file_node = get_inode_by_id(file);

    if (file_node == 0)
        return -1;
    serial_trace("found file at inode : %d\n", file_node->id);

    uint8_t _flags = flags;
    if (_flags == 0)
        _flags = O_RDWR;

    int fd = descriptor_add(file, file_node->entry, file_node->entry->addr, 0, _flags);
    serial_trace("fd : %d\n", fd);

    return fd;
}

int vfs_fstat(int fd, struct vfs_file_stats *buf) {
    struct file_descriptor *descriptor = descriptor_get(fd);
    if (descriptor == 0)
        return -1;

    vfs_inode_t *inode = get_inode_by_id(descriptor->inode);

    buf->name = inode->entry->name;
    buf->size = inode->size;
    serial_trace("entry name : %s, on buff 0x%x\n", buf->name, buf);
    return 0;
}

int vfs_read(int fd, void *buf, size_t count) {
    // serial_trace ("vfs read : %d\n", fd);
    struct file_descriptor *descriptor = descriptor_get(fd);
    if (descriptor == 0) {
        serial_trace("descriptor not found\n");
        return -1;
    }

    if (buf == 0) {
        serial_trace("buf not found\n");
        return -2;
    }
    // cek apakah fd nya puya akses read
    if (!(descriptor->flags & FD_FLAG_READ))
        return -3;

    // descriptor->file->inode->fs->ops->read(

    // )

    uintptr_t addr = descriptor->addr;
    memcopy(buf, (void *)(addr + descriptor->offset), count);
    return 0;
}

int vfs_close(int fd) {
    struct file_descriptor *descriptor = descriptor_get(fd);
    if (descriptor == 0)
        return -1;

    descriptor_free(fd);
    return 0;
}
