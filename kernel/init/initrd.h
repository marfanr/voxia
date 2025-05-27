#ifndef __INIT__INITRD_H__
#define __INIT__INITRD_H__

#include <hal/block/block.h>
#include <libk/type.h>
#include <vfs/vfs.h>
typedef struct initrd_module
{
    size_t size;
    uint64_t start;
} initrd_module_t;

typedef struct initrd_file
{
    char name[100];
    uint32_t size;
    uint64_t data;
} initrd_file_t;

char *initrd_load(initrd_module_t module, const char *name);
vfs_operations_t *initrd_vfs_impl(initrd_module_t *module);
block_device_operations_t *initrd_block_impl(initrd_module_t *module);
#endif // __INIT__INITRD_H__
