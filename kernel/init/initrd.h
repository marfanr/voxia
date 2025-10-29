#ifndef __INIT__INITRD_H__
#define __INIT__INITRD_H__

#include <hal/block/block.h>
#include <libk/type.h>
#include <vfs/filesystem.h>
#include <vfs/vfs.h>

typedef struct initrd_module
{
    size_t   size;
    uint64_t start;
} initrd_module_t;

typedef struct initrd_file
{
    char     name[100];
    uint32_t size;
    uint64_t data;
} initrd_file_t;

char                      *initrd_load(initrd_module_t module, const char *name);
filesystem_t              *initrd_fs_impl();
block_device_operations_t *initrd_block_impl();

#endif // __INIT__INITRD_H__
