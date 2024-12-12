#ifndef __SYS__DESCRIPTOR_H__
#define __SYS__DESCRIPTOR_H__

#include <libk/type.h>

typedef struct file_descriptor {
	uint64_t offset;
	uint64_t inode;
	uint64_t flags;
} file_descriptor_t;

void add_file_descriptor(file_descriptor_t *fd, uint64_t inode, uint64_t flags);

#endif // __SYS__DESCRIPTOR_H__
