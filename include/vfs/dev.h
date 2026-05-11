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

#ifndef __HAL__BLOCK__BLOCK_H__
#define __HAL__BLOCK__BLOCK_H__

#include "procc/thread.h"
#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char dev_name_t[128];

enum {
	ERR_DEV_OPS_NOT_IMPLEMENTED = -3,
	DEV_OK = 1,
};

typedef struct {
	int (*open)(void* data, int op_mode, thread_t* thread);
	int (*read)(void* data, uintptr_t path, void* buf, size_t count);
	int (*write)(void* data, uintptr_t path, void* buf, size_t count);
	int (*close)(void* data);
	void* data;
} cdev_operations_t;

typedef struct cdev {
	dev_name_t name;
	uint16_t major;
	uint16_t minor;
	uint32_t uuid;
	uint16_t permission;
	cdev_operations_t* ops;
	struct cdev* next;
} __attribute__((aligned(64))) cdev_t;
typedef cdev_t* cdev_ptr_t;

int vxMakeDev(cdev_operations_t* ops, uint16_t minor, uint32_t uuid,
	      uint16_t permission, dev_name_t name);
cdev_ptr_t vxRetrieveDev(uint16_t major, uint16_t minor);

#ifdef __cplusplus
}
#endif

#endif // __HAL__BLOCK__BLOCK_H__
