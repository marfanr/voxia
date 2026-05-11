#ifndef __VFS__FILE_H__
#define __VFS__FILE_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct file file_t;

typedef struct {
	size_t (*read)(struct file*, char* buffer, size_t size);
} file_operations_t;

struct file {
	file_operations_t* ops;
	void* private_data;
};

#ifdef __cplusplus
}
#endif

#endif // __VFS__FILE_H__