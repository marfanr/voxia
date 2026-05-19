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

#include <type.h>

#define DEV_MAJOR_MAX_COUNT 256
#define DEV_MINOR_BITMAP_COUNT 32

#define DEV_MAJOR_MEM          1   // /dev/mem, /dev/null, /dev/zero
#define DEV_MAJOR_FD           2   // floppy
#define DEV_MAJOR_HD           3   // IDE HDD
#define DEV_MAJOR_TTY          4   // tty devices
#define DEV_MAJOR_CONSOLE      5   // console
#define DEV_MAJOR_LP           6   // parallel printer
#define DEV_MAJOR_VCS          7   // virtual console
#define DEV_MAJOR_SCSI_DISK    8   // SCSI/SATA disk
#define DEV_MAJOR_MD           9   // ramdisk/md
#define DEV_MAJOR_NET          10  // network misc

#define DEV_MAJOR_CDROM        11  // ATAPI CD/DVD
#define DEV_MAJOR_INPUT        12  // keyboard/mouse/input
#define DEV_MAJOR_AUDIO        13  // audio devices
#define DEV_MAJOR_USB          14  // generic USB
#define DEV_MAJOR_FB           15  // framebuffer/gpu
#define DEV_MAJOR_PTY          16  // pseudo terminal
#define DEV_MAJOR_SERIAL       17  // serial/uart
#define DEV_MAJOR_RTC          18  // rtc/timer
#define DEV_MAJOR_NVME         19  // NVMe storage
#define DEV_MAJOR_LOOP         20  // loopback block device

#ifdef __cplusplus
extern "C" {
#endif

typedef char dev_name_t[128];

enum {
	ERR_DEV_OPS_NOT_IMPLEMENTED = -3,
	DEV_OK = 1,
};

struct  vops_blk;
typedef struct cdev {
	uint32_t major;
	uint32_t minor;
	struct  vops_blk* ops;

	struct cdev* next;
} __attribute__((aligned(64))) cdev_t;
typedef cdev_t* cdev_ptr_t;

cdev_ptr_t create_dev(struct  vops_blk* ops, uint32_t major);
cdev_ptr_t retrieve_dev(uint32_t major, uint32_t minor);

#ifdef __cplusplus
}
#endif

#endif // __HAL__BLOCK__BLOCK_H__
