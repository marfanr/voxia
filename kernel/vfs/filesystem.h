#ifndef __VFS__FILESYSTEM_H__
#define __VFS__FILESYSTEM_H__

#include <libk/type.h>

typedef struct vfs_operations vfs_operations_t;

typedef struct filesystem
{
    const char         name[16];
    vfs_operations_t  *ops;
    struct filesystem *next;
} filesystem_t;

void          filesystem_register(const char *name, filesystem_t *fs);
filesystem_t *filesystem_find(const char *name);

#endif // __VFS__FILESYSTEM_H__