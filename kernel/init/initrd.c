#include "initrd.h"
#include "hal/block/block.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/type.h"
#include "libk/vector.h"
#include "memory/kalloc.h"
#include "memory/memory_utils.h"
#include "memory/phys_base_allocator.h"
#include "vfs/dentry.h"
#include "vfs/filesystem.h"
#include <libk/fs/tar.h>

#include <libk/serial.h>
#include <libk/str.h>
#include <memory/kalloc.h>
#include <memory/slab.h>
#include <memory/vm_manager.h>
#include <sys/descriptor.h>
#include <vfs/vfs.h>

// local use
static initrd_module_t *initrd_module = 0;

static int
initrd_oct2bin(unsigned char *str, int len)
{
    int            n = 0;
    unsigned char *c = str;
    while (len-- > 0)
    {
        n *= 8;
        n += *c - '0';
        c++;
    }
    return n;
}

// /**
//  * @brief load file from initrd
//  *
//  * @param module initrd module
//  * @param name file name
//  * @return char* file data
//  */
// char *
// initrd_load(initrd_module_t module, const char *name)
// {
//     uint8_t   *addr   = (uint8_t *)module.start;
//     TarHeader *header = (TarHeader *)addr;

//     if (strncmp(name, "/", 1) == 0)
//     {
//         name++;
//     }

//     while (strncmp(header->ustar, "ustar", 5) == 0)
//     {
//         int size = initrd_oct2bin(header->size, 11);
//         if (strncmp(header->filename, name, sizeof(name)) == 0)
//         {
//             serial_trace("\nfile %s found\n", header->filename);
//             if (header->typeflag == '5')
//             { // directory
//                 uint8_t *subaddr = addr + 512;
//                 header           = (TarHeader *)subaddr;
//                 serial_trace("file %s loaded from subdir\n", header->filename);
//                 // continue;
//                 char *out = subaddr + 512;
//                 return out;
//             }

//             header    = (TarHeader *)addr;
//             char *out = addr + 512;
//             serial_trace("file %s loaded\n", header->filename);
//             serial_trace("file size : %d\n", size);
//             serial_trace("file gid : 0x%x\n", initrd_oct2bin(header->gid, 8));
//             return out;
//         }
//         addr += (((size + 511) / 512) + 1) * 512;
//         header = (TarHeader *)addr;
//     }
//     return 0;
// }`

INIT(initrd)
{
    initrd_module = &ctx->initrd_module;
    paging_mmap_fill(paging_get_highest_page_map(), 0xFFFFD00000000000,
                     VIRT2PHYS(initrd_module->start), initrd_module->size / BLOCK_SIZE, 0b111);
    initrd_module->start = 0xFFFFD00000000000;
    LOG_INFO("INITRD", "initrd module found at 0x%x", initrd_module->start);

    // registering initrd
    block_register_device("/block/initrd", initrd_block_impl(), 0);
    filesystem_register("initrd", initrd_fs_impl());
    vfs_mount("/dev/initrd", "/block/initrd", "initrd");
    LOG_INFO("INITRD", "initrd registered");
}

static uint8_t *
initrd_block_read(uint64_t offset, size_t _count)
{
    // we dont need copy the whole file, just return the address
    // because the initrd is read-only
    uint8_t *addr = (uint8_t *)(initrd_module->start + offset);
    return addr;
}

int
initrd_block_write(uint64_t offset, size_t count, uint8_t *data)
{
    return BLOCK_OPT_NOT_IMPLEMENTED;
}

open_response_code
initrd_block_open(fmode_t mode)
{
    return OPEN_SUCCESS;
}

block_device_operations_t *
initrd_block_impl()
{

    block_device_operations_t *ops =
        (block_device_operations_t *)kalloc(sizeof(block_device_operations_t));
    ops->read = initrd_block_read;
    ops->open = initrd_block_open;
    // ops->write = initrd_block_write;
    return ops;
}

int
initrd_lookup(vfs_inode_t *dir, dentry_ptr dentry)
{
    // ini tidk diperlukan karena semua file sudah di inde dari awal
    return 0;
}

void
initrd_mount(vfs_inode_t *inode, dentry_ptr dentry)
{
    block_device *dev    = inode->block;
    uint64_t      offset = 0;
    uint8_t      *addr   = (uint8_t *)dev->ops->read(offset, 0);
    TarHeader    *header = (TarHeader *)addr;

    // serial_trace("header at 0x%x\n", header);

    while (strncmp(header->ustar, "ustar", 5) == 0)
    {
        dentry_ptr curr_entry = dentry;

        size_t l = strlen(header->filename);
        if (header->filename[l - 1] == '\n')
            l--;

        int size = initrd_oct2bin(header->size, 11);
        // serial_trace("file name %s, size : %d\n", header->filename, size);

        // exploding path
        vector(string) exploded_path = {0};
        vector_init(&exploded_path);
        explode(header->filename, '/', &exploded_path);

        for (size_t i = 0; i < exploded_path.size; i++)
        {
            // serial_trace("      path %d = %s \n", i, exploded_path.data[i]->c_str);

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
                vfs_inode_t *child_inode = vfs_create_inode(inode->fs);
                child_inode->block       = dev;
                // child_inode->is_directory = true;

                // if (i == exploded_path.size - 1)
                // {
                child_inode->is_directory = header->typeflag == '5';
                child_inode->permission   = initrd_oct2bin(header->mode, 8);
                child_inode->size         = size;
                child_inode->offset       = offset + 512;
                child_inode->block->bar   = (uintptr_t)initrd_module->start;
                // child_inode->
                // }

                dentry_ptr new_entry =
                    create_directory_entry(exploded_path.data[i], child_inode, curr_entry);
                curr_entry = new_entry;
                // serial_trace("      | created new entry\n");
            }
        }

        vector_destroy(&exploded_path);

        offset += (((size + 511) / 512) + 1) * 512;
        header = (TarHeader *)dev->ops->read(offset, 0);
    }
}

filesystem_t *
initrd_fs_impl()
{
    filesystem_t *fs = (filesystem_t *)kalloc(sizeof(filesystem_t));
    strcpy((char *)fs->name, "initrd");
    fs->ops         = (vfs_operations_t *)kalloc(sizeof(vfs_operations_t));
    fs->ops->lookup = initrd_lookup;
    fs->ops->mount  = initrd_mount;

    return fs;
}
