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

#ifndef __BLOCK__BLOCK_H__
#define __BLOCK__BLOCK_H__

#include <type.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
	FMODE_R = 1UL << 0,
	FMODE_W = 1UL << 1,
	FMODE_E = 1UL << 2
} fmode_t;

typedef enum { OPEN_SUCCESS = 0, OPEN_FAILED = 1 } open_response_code;

typedef struct {
	open_response_code (*open)(fmode_t mode);
	int (*close)(void);
	uint8_t* (*read)(uint64_t offset, size_t count);
	int (*write)(void* block, uint64_t offset, size_t count, uint8_t* data);
} __attribute__((aligned(64))) block_device_operations_t;

void registerBlockDevice(const char* name, block_device_operations_t* ops,
			 void* identifier);
#ifdef __cplusplus
}
#endif

#endif // __BLOCK__BLOCK_H__