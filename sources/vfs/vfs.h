
#ifndef __VFS__VFS_H__
#define __VFS__VFS_H__

#include <libk/type.h>

typedef struct vfs_node {
  char name[256];
  uint64_t inode;
  uint64_t size;
  uint64_t flags;
} vfs_node_t;

#endif // __VFS__VFS_H__
