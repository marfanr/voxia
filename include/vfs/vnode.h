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
typedef struct thread thread_t;

typedef struct vops_file {
	int (*ioctl)(vnode_t* vnode, uint32_t req, void* arg);
	int (*read)(vnode_t* vnode, void* buf, size_t len, size_t offset);
	long (*write)(vnode_t* vnode, void* buf, size_t len, size_t offset);
	int (*poll)(vnode_t* vnode, struct thread* waiter);
	int (*flush)(vnode_t* vnode);
	int (*truncate)(vnode_t* vnode, size_t length);
	int (*readdir)(vnode_t* vnode, void* buf, size_t len, uint64_t* pos);
	void (*redirect_on_open)(vnode_t* vnode, void* fd);
} vops_file_t;

typedef struct {
	int (*readlink)(vnode_t* vnode, char* buf, size_t bufsize);
} vops_lnk_t;
typedef struct vops_blk {
	int (*ioctl)(vnode_t* vnode, uint32_t req, void* arg);
	int (*open)(vnode_t* vnode, int op_mode, thread_t* thread);
	int (*read)(vnode_t* vnode, uintptr_t addr, void* buf, size_t count);
	int (*write)(vnode_t* vnode, uintptr_t addr, void* buf, size_t count);
	int (*close)(vnode_t* vnode);
	int (*flush)(vnode_t* vnode);
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

/* vnode flags */
#define VNODE_NO_BACKING 0x3;


struct vnode {
	atomic_t refcount;
	vnode_id_t id;
	uint8_t type;
	size_t size;
	void* ops;
	uint16_t permission;
	uint16_t flags;

	struct fs_instance* fs_instance;
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