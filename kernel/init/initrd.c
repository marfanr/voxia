#include "initrd.h"
#include "libk/str/memcopy.h"
#include <libk/fs/tar.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <libk/str/strncmp.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <sys/descriptor.h>
#include <vfs/vfs.h>

/**
 * @brief Mengonversi bilangan oktal menjadi bilangan biner.
 *
 * Fungsi ini mengonversi string yang mewakili bilangan oktal menjadi bilangan
 * biner. Bilangan oktal direpresentasikan sebagai string dengan
 * karakter-karakter '0' hingga '7'. Setiap karakter diubah menjadi bilangan
 * desimal dan kemudian diubah menjadi bilangan biner.
 *
 * @param str Pointer ke string yang mewakili bilangan oktal.
 * @param len Panjang string yang mewakili bilangan oktal.
 * @return Bilangan biner yang dihasilkan dari konversi.
 */
int initrd_oct2bin(unsigned char *str, int len) {
  int n = 0;
  unsigned char *c = str;
  while (len-- > 0) {
    n *= 8;
    n += *c - '0';
    c++;
  }
  return n;
}

/**
 * @brief load file from initrd
 *
 * @param module initrd module
 * @param name file name
 * @return char* file data
 */
char *initrd_load(initrd_module_t module, const char *name) {
  uint8_t *addr = (uint8_t *)module.start;
  TarHeader *header = (TarHeader *)addr;

  if (strncmp(name, "/", 1) == 0) {
    name++;
  }

  while (strncmp(header->ustar, "ustar", 5) == 0) {
    int size = initrd_oct2bin(header->size, 11);
    if (strncmp(header->filename, name, sizeof(name)) == 0) {
      serial_trace("\nfile %s found\n", header->filename);
      if (header->typeflag == '5') { // directory
        uint8_t *subaddr = addr + 512;
        header = (TarHeader *)subaddr;
        serial_trace("file %s loaded from subdir\n", header->filename);
        // continue;
        char *out = subaddr + 512;
        return out;
      }

      header = (TarHeader *)addr;
      char *out = addr + 512;
      serial_trace("file %s loaded\n", header->filename);
      serial_trace("file size : %d\n", size);
      serial_trace("file gid : 0x%x\n", initrd_oct2bin(header->gid, 8));
      return out;
    }
    addr += (((size + 511) / 512) + 1) * 512;
    header = (TarHeader *)addr;
  }
  return 0;
}

static char **explode_path(const char *path) {
  // serial_trace ("exploding path : %s\n", path);
  char **result =
      (char **)(phys_base_alloc(1 + sizeof(char *) * strlen(path) / 4096));
  memset(result, 0, sizeof(char *) * strlen(path));

  int i = 0, j = 0;
  char *buffer = (char *)(phys_base_alloc(1 + 256 / 4096));
  memset(buffer, 0, 1 + 256);
  while (*path) {
    if (*path == '/') {
      buffer[i] = 0;
      result[j] = (char *)(phys_base_alloc(1 + sizeof(buffer) / 4096));
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
    memcopy(result[j], buffer, strlen(buffer));
    // serial_trace ("result[%d] : %s\n", j, result[j]);
    j++;
  }
  phys_base_free((void *)(uint64_t)buffer, 1 + 256 / 4096);
  return result;
}

static int calc_path_depth(const char *path) {
  int depth = 0;
  while (*path) {
    if (*path == '/') {
      depth++;
    }
    path++;
  }
  return depth + 1;
}

uint8_t *initrd_block_read(void *this, uint64_t offset, size_t _count) {
  // no used in initrd
  block_device_operations_t *_this = (block_device_operations_t *)this;
  initrd_module_t *module = (initrd_module_t *)_this->ctx;
  uint8_t *addr = (uint8_t *)(module->start + offset);
  return addr;
}

int initrd_block_write(block_device_operations_t *this, uint64_t offset,
                       size_t count, uint8_t *data) {
  return BLOCK_OPT_NOT_IMPLEMENTED;
}

block_device_operations_t *initrd_block_impl(initrd_module_t *module) {
  block_device_operations_t *ops =
      (block_device_operations_t *)(phys_base_alloc(
          1 + sizeof(block_device_operations_t) / 4096));
  ops->ctx = (void *)module;
  ops->read = initrd_block_read;
  return ops;
}

struct vfs_open_response *initrd_open(block_device_operations_t *block_op,
                                      const char *path, int _inode) {
  _inode; // not used
  serial_trace("lookup %s\n", path);
  int offset = 0;
  TarHeader *header = (TarHeader *)block_op->read(block_op, offset, 0);
  while (strncmp(header->ustar, "ustar", 5) == 0) {
    int size = initrd_oct2bin(header->size, 11);

    if (strncmp(header->filename, path, strlen(path)) == 0) {
      struct vfs_open_response *response =
          (struct vfs_open_response *)(phys_base_alloc(
              1 + sizeof(struct vfs_open_response) / 4096));
      memset(response, 0, sizeof(struct vfs_open_response) / 4096);

      response->size = size;
      response->permission = initrd_oct2bin(header->mode, 8);
      response->is_directory = header->typeflag == '5';
      response->addr = (uintptr_t)block_op->read(block_op, offset + 512, 0);

      serial_trace("loaded file name : %s from 0x%x \n", header->filename,
                   response->addr);

      return response;
    }
    offset += (((size + 511) / 512) + 1) * 512;
    header = (TarHeader *)block_op->read(block_op, offset, 0);
  }
  return 0;
}

int initrd_read(block_device_operations_t *block_op, int inode, void *buf,
                size_t count) {
  return 0;
}

/**
 * this will  no longer used in the initrd because all the structures inside
 *  the initd have already been copied into the VFS node
 * @return 0 not found
 */
int initrd_lookup(vfs_inode_t *node, struct vfs_entry *entry,
                  vfs_inode_t **output) {
  return 0;
}

int initrd_mount(vfs_inode_t *node) {
  // construct vfs tree from initrd archive
  serial_trace("initrd mount\n");

  uint64_t current_offset = 0;

  TarHeader *header =
      (TarHeader *)node->block->ops->read(node->block->ops, current_offset, 0);

  while (strncmp(header->ustar, "ustar", 5) == 0) {
    int size = initrd_oct2bin((unsigned char *)header->size, 11);

    // skip processing if it is a directory to reduce loop
    if (header->typeflag == '5')
      goto mount_end;

    char *path = header->filename;

    // if (strncmp(header->filename, entry->name, sizeof(entry->name)) == 0) {
    // serial_trace("ON MOUNT: found path %s\n", header->filename);
    int path_depth = calc_path_depth(path);
    vfs_inode_t *curr_inode = node;
    struct vfs_entry *curr_entry = node->entry;
    char **exploded_path = explode_path(path);

    for (int i = 0; i < path_depth; i++) {
      for (int j = 0; j < curr_entry->child_count; j++) {
        if (strncmp(curr_entry->child[j]->name, exploded_path[i],
                    strlen(path)) == 0) {

          curr_entry = curr_entry->child[j];
          // curr_inode = curr_entry->inode;
          goto continue_next_path;
          break;
        }
      }

      // serial_trace("not found child %s\n", exploded_path[i]);
      // create new entry
      curr_inode = vfs_create_inode(vfs_last_existing_inode++, node->fs);
      curr_inode->size = size;

      if (i != (path_depth - 1)) {
        curr_inode->is_directory = 1;
      }

      struct vfs_entry *entry =
          vfs_create_entry(exploded_path[i], curr_inode, curr_entry);

      curr_inode->entry = entry;
      entry->addr = (uintptr_t)node->block->ops->read(node->block->ops,
                                                      current_offset + 512, 0);
      if (curr_entry->child_count == 0)
        curr_entry->child = (struct vfs_entry **)(phys_base_alloc(
            1 + sizeof(struct vfs_entry *) / 4096));

      curr_entry->child_count++;
      curr_entry->child[curr_entry->child_count - 1] = entry;
      curr_entry = entry;

    continue_next_path:
      continue;
    }

  mount_end:
    current_offset += (((size + 511) / 512) + 1) * 512;
    header = (TarHeader *)node->block->ops->read(node->block->ops,
                                                 current_offset, 0);
  }
  return 0;
}

vfs_operations_t *initrd_vfs_impl(initrd_module_t *module) {
  // TODO: migrate to proper calloc
  vfs_operations_t *ops =
      (vfs_operations_t *)phys_base_alloc(1 + sizeof(vfs_operations_t) / 4096);
  ops->open = initrd_open;
  ops->read = initrd_read;
  ops->lookup = initrd_lookup;
  ops->mount = initrd_mount;
  return ops;
}
