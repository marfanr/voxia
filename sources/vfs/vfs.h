
#ifndef __VFS__VFS_H__
#define __VFS__VFS_H__

#include <hal/block/block.h>
#include <libk/type.h>
#include <sys/descriptor.h>

struct vfs_open_response
{
    boolean_t is_directory;
    int id;
    int permission;
    int size;
    uintptr_t addr;
};

typedef struct
{
    struct vfs_open_response *(*open) (block_device_operations_t *block_op,
                                       const char *path, int inode);
    int (*read) (block_device_operations_t *block_op, int inode, void *buf,
                 size_t count);
    int (*write) (block_device_operations_t *block_op, const void *buf,
                  size_t count);
    int (*lseek) (int fd, long offset, int whence);
    int (*stat) (const char *path, struct stat *buf);
    int (*fstat) (int fd, struct stat *buf);
    int (*ioctl) (int fd, unsigned long request, void *arg);
    int (*mmap) (void *addr, size_t length, int prot, int flags, int fd,
                 long offset);
    int (*munmap) (void *addr, size_t length);
    int (*mprotect) (void *addr, size_t length, int prot);
    int (*ftruncate) (int fd, long length);
    int (*fsync) (int fd);
    int (*fallocate) (int fd, int mode, long offset, long len);
    int (*readdir) (int fd, struct dirent *dirp);
    int (*mkdir) (const char *pathname, int mode);
    int (*rmdir) (const char *pathname);
} vfs_operations_t;

struct vfs_fs
{
    boolean_t has_own_inode;
    const char *name;
    vfs_operations_t *ops;
};

typedef struct
{
    boolean_t is_mounted;
    boolean_t is_directory;
    uint32_t permission;
    uint32_t ref_count;
    size_t size;
    uint64_t id;
    struct vfs_fs *fs;
    struct vfs_entry *entry;
} vfs_inode_t;

typedef enum
{
    RBT_RED,
    RBT_BLACK
} node_color;

struct vfs_rb_node
{
    vfs_inode_t *inode;
    struct vfs_rb_node *left;
    struct vfs_rb_node *right;
    struct vfs_rb_node *parent;
    node_color color;
};

#define VFS_ENTRY_PRE_ALLOC 5

struct vfs_entry
{
    int child_count;
    char *name;
    vfs_inode_t *inode;
    struct vfs_entry *parent;
    struct vfs_entry **child;
    uintptr_t addr;
};

struct vfs_mount
{
    vfs_inode_t *inode; // root inode
    struct vfs_fs *fs;
    struct block_device *block;
    struct vfs_mount *next;
};

struct vfs_path_cache_item
{
    char *path;
    int used_count;
    vfs_inode_t *inode;
    struct vfs_entry *entry;
    struct vfs_path_cache_item *next;
};

struct vfs_path_cache
{
    struct vfs_path_cache_item *cache;
};

typedef struct
{
    struct vfs_rb_node *inode_root;
    struct vfs_entry *entry_root;
    struct vfs_mount *mount_list;
    struct vfs_fs **fs_list;
    struct vfs_path_cache **path_cache;
} vfs_t;

void vfs_install ();
void vfs_register_fs (const char *name, vfs_operations_t *ops,
                      boolean_t has_own_inode);
int vfs_mount (const char *path, const char *block, const char *fs);
int vfs_umount (const char *path);

int vfs_lookup (const char *path, int inode);

#define O_RDONLY FD_FLAG_READ
#define O_WRONLY FD_FLAG_WRITE
#define O_RDWR (FD_FLAG_READ | FD_FLAG_WRITE)

/**
 * Membuka file atau perangkat yang ditentukan oleh path yang diberikan dengan
 * flag yang ditentukan.
 *
 * @param path path file yang ingin di buka
 * @param flags
 * @return The file descriptor dari file yang dibuka , VFS_ERR_NOT_FOUND jika
 * tidak ditemukan
 */
int vfs_open (const char *path, uint8_t flags);

struct vfs_file_stats
{
    const char *name;
    size_t size;
};

/**
 * Mendapatkan informasi file yang di tunjuk oleh file descriptor yang
 * diberikan.
 *
 * @param fd file descriptor yang ingin di dapatkan informasinya
 * @param buf buffer yang akan di isi dengan informasi file
 * @return 0 jika berhasil, VFS_ERR_INVALID_FD jika fd tidak valid
 */
int vfs_fstat (int fd, struct vfs_file_stats *buf);

/**
 * Menutup file descriptor yang diberikan.
 *
 * @param fd file descriptor yang ingin di tutup
 * @return 0 jika berhasil, VFS_ERR_INVALID_FD jika fd tidak valid
 */
int vfs_close (int fd);

/**
 * Membaca file yang di tunjuk oleh file descriptor yang diberikan.
 *
 * @param fd file descriptor yang ingin di baca
 * @param buf buffer yang akan di isi dengan data yang di baca
 * @param count jumlah byte yang ingin di baca
 * @return jumlah byte yang berhasil di baca, VFS_ERR_INVALID_FD jika fd tidak
 * valid
 */
int vfs_read (int fd, void *buf, size_t count);

/**
 * Menulis data ke file yang di tunjuk oleh file descriptor yang diberikan.
 *
 * @param fd file descriptor yang ingin di tulis
 * @param buf buffer yang berisi data yang akan di tulis
 * @param count jumlah byte yang ingin di tulis
 * @return jumlah byte yang berhasil di tulis, VFS_ERR_INVALID_FD jika fd tidak
 * valid
 */
int vfs_write (int fd, const void *buf, size_t count);

/**
 * menutup file descriptor yang diberikan.
 *
 * @param fd file descriptor yang ingin di tutup
 * @return 0 jika berhasil, VFS_ERR_INVALID_FD jika fd tidak valid
 */
int vfs_close (int fd);

void vfs_caching_path (const char *path, vfs_inode_t *inode,
                       struct vfs_entry *entry);

#define VFS_ERR_NOT_FOUND -1
#define VFS_ERR_INVALID_PATH -2
#define VFS_ERR_INVALID_FD -3

#define VFS_MOUNT_ALREADY_MOUNTED -1
#define VFS_MOUNT_NOT_FOUND -2
#define VFS_MOUNT_INVALID_FS -3

#endif // __VFS__VFS_H__
