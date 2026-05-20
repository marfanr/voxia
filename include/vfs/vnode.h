// Copyright (c) 2025 Mohammad Arfan

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef __VFS__VNODE_H__
#define __VFS__VNODE_H__

#include "vfs/dentry.h"
#include "vfs/dev.h"
#include "vfs/filesystem.h"
#include <llist.h>
#include <type.h>
#include <vector.h>

enum {
	VNODE_TYPE_FILE = 1,
	VNODE_TYPE_DIR = 2,
	VNODE_TYPE_DEV = 3,
	VNODE_TYPE_CHR = 4,
	VNODE_TYPE_BLK = 5,
	VNODE_TYPE_FIFO = 6,
	VNODE_TYPE_SOCK = 7,
	VNODE_TYPE_LNK = 8
};

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vnode vnode_t;
typedef struct {
	int (*read)(vnode_t* vnode, void* buf, size_t len, size_t offset);
} vops_file_t;

typedef struct thread thread_t;
typedef struct  vops_blk{
	int (*open)(void* vdata, int op_mode, thread_t* thread);
	int (*read)(void* vdata, uintptr_t addr, void* buf, size_t count);
	int (*write)(void* vdata, uintptr_t addr, void* buf, size_t count);
	int (*close)(void* vdata);
	void* v_data;
} vops_blk_t;

typedef uint64_t vnode_id_t;

/*
General VNode
*/
struct device_id {
	uint32_t major;
	uint32_t minor;
};
struct vnode {
	atomic_t refcount;
	vnode_id_t id;
	uint8_t type;
	size_t size;
	void* ops;
	uint32_t uuid;
	uint16_t permission;

	filesystem_t* fs;
	cdev_ptr_t mountedhere;
	cdev_ptr_t mount;

	union {
		void* vnode_private;
		struct device_id device;
	};
} __attribute__((aligned(64)));

typedef struct vnode* vnode_ptr_t;

vnode_ptr_t KERNEL_API create_vnode();
void vxFreeVnode(vnode_ptr_t vnode);

#ifdef __cplusplus
}
#endif

#endif // __VFS__VNODE_H__