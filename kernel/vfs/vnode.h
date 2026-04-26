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

#include "libk/vector.h"
#include "procc/thread.h"
#include "type.h"
#include "vfs/dentry.h"

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

typedef struct vnode vnode_t;
typedef struct {
	int (*read)(vnode_t* vnode, void* buf, size_t len, size_t offset);
} vops_file_t;

typedef struct {
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
struct vnode {
	vnode_id_t id;
	uint8_t type;
	size_t size;
	void* ops;
	vector(dentry_ptr) dentry_list;
	uint32_t uuid;
	uint16_t permission;

	void* fs;
	void* private;
};
typedef struct vnode* vnode_ptr_t;

void vxFreeVnode(vnode_ptr_t vnode);

#endif // __VFS__VNODE_H__