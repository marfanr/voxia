
#ifndef __VFS__VFS_H__
#define __VFS__VFS_H__

#include "libk/string.h"
#include "vfs/dentry.h"
#include "vfs/file.h"
#include "vfs/filesystem.h"
#include <hal/block/block.h>
#include <libk/type.h>
#include <libk/vector.h>

#define OP_NOT_IMPLEMENTED 0

typedef enum
{
    VFS_ERR_MOUNT_NOT_FOUND       = -1,
    VFS_ERR_MOUNT_ALREADY_MOUNTED = -2,
    VFS_ERR_MOUNT_NOT_MOUNTED     = -3,
    VFS_ERR_MOUNT_INVALID_PATH    = -4,
    VFS_ERR_MOUNT_INVALID_BLOCK   = -5,
    VFS_ERR_MOUNT_INVALID_FS      = -6,
} vfs_mount_error;

struct vfs_open_response
{
    boolean_t is_directory;
    int       id;
    int       permission;
    int       size;
    uintptr_t addr;
};

typedef struct vfs_inode vfs_inode_t;
struct vfs_inode
{
    boolean_t            is_mounted;
    boolean_t            is_directory;
    uint32_t             permission;
    uint32_t             ref_count;
    size_t               size;
    uint64_t             id;
    filesystem_t        *fs;
    file_operations_t   *file_ops;
    struct block_device *block;
    vector(dentry_ptr) dentry_list;
    uint64_t offset;
};

typedef struct vfs_operations
{
    struct vfs_open_response *(*open)(block_device_operations_t *block_op, const char *path,
                                      int inode);
    int (*lookup)(vfs_inode_t *dir, dentry_ptr dentry);
    void (*mount)(vfs_inode_t *dir, dentry_ptr dentry);
    int (*read)(block_device_operations_t *block_op, int inode, void *buf, size_t count);
    int (*write)(block_device_operations_t *block_op, const void *buf, size_t count);
    int (*lseek)(int fd, long offset, int whence);
    int (*stat)(const char *path, struct stat *buf);
    int (*fstat)(int fd, struct stat *buf);
    int (*ioctl)(int fd, unsigned long request, void *arg);
    int (*mmap)(void *addr, size_t length, int prot, int flags, int fd, long offset);
    int (*munmap)(void *addr, size_t length);
    int (*mprotect)(void *addr, size_t length, int prot);
    int (*ftruncate)(int fd, long length);
    int (*fsync)(int fd);
    int (*fallocate)(int fd, int mode, long offset, long len);
    int (*readdir)(int fd, struct dirent *dirp);
    int (*mkdir)(const char *pathname, int mode);
    int (*rmdir)(const char *pathname);
} __attribute__((aligned(64))) vfs_operations_t;

typedef uint8_t vfs_open_mode;
enum
{
    OPEN_MODE_R   = 1U << 0,
    OPEN_MODE_W   = 1U << 1,
    OPEN_MODE_X   = 1U << 2,
    OPEN_MODE_ALL = OPEN_MODE_R | OPEN_MODE_W | OPEN_MODE_X
};

struct vfs_inode *vfs_create_inode(filesystem_t *fs);
int               vfs_mount(const char *path, const char *block, const char *fs);
vfs_mount_error   vfs_umount(const char *path);

int     vxVFSOpen(const char *path, uint8_t flags);
file_t *vxFileInternalOpen(const char *path, uint8_t flags);
struct vfs_file_stats
{
    string name;
    size_t size;
};
typedef struct vfs_file_stats *vfs_file_stats_ptr;
int                            vxVFSFileStat(file_t *file, vfs_file_stats_ptr buf);
int                            vxVFSClose(int fd);
int                            vxVFSRead(file_t *file, void *buf, size_t count);
int                            vfs_write(int fd, const void *buf, size_t count);
int                            vxVFSClose(int fd);

void vfs_caching_path(const char *path, vfs_inode_t *inode, struct vfs_entry *entry);

#endif // __VFS__VFS_H__
