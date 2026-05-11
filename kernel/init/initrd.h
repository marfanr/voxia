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

#ifndef __INIT__INITRD_H__
#define __INIT__INITRD_H__

#include <type.h>
#include <vfs/dev.h>
#include <vfs/filesystem.h>
#include <vfs/vfs.h>

typedef struct initrd_module {
	size_t size;
	uint64_t start;
} initrd_module_t;

typedef struct initrd_file {
	char name[100];
	uint32_t size;
	uint64_t data;
} initrd_file_t;

char* initrd_load(initrd_module_t module, const char* name);

#endif // __INIT__INITRD_H__
