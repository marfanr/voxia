#ifndef __VFS__ENUM_H__
#define __VFS__ENUM_H__

enum {
	VFS_OK = 0,
	VFS_ERR_BUSY = -1,
	VFS_ENOENT = -2,
	VFS_ERR = -5,
	VFS_DEV_NOT_FOUND = -7,
	VFS_FS_NOT_FOUND = -8,
};

#endif // __VFS__ENUM_H__