#ifndef __SYS__DESCRIPTOR_H__
#define __SYS__DESCRIPTOR_H__

#include "vfs/dentry.h"
#include "vfs/file.h"
#include <type.h>
#include <vfs/vfs.h>

#define FD_FLAG_READ 1
#define FD_FLAG_WRITE 1 << 1

typedef struct file_descriptor {
	file_t* file;
	size_t max_file;
} file_descriptor_t;

typedef int fd_t;

#endif // __SYS__DESCRIPTOR_H__
