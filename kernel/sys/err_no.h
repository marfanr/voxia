#ifndef __SYS__ERR_NO_H__
#define __SYS__ERR_NO_H__

#define EPERM 1    /* Operation not permitted */
#define ENOENT 2   /* No such file or directory */
#define ESRCH 3    /* No such process */
#define EINTR 4    /* Interrupted system call */
#define EIO 5      /* I/O error */
#define ENXIO 6    /* No such device or address */
#define E2BIG 7    /* Argument list too long */
#define ENOEXEC 8  /* Exec format error */
#define EBADF 9    /* Bad file descriptor */
#define ECHILD 10  /* No child processes */
#define EAGAIN 11  /* Resource temporarily unavailable */
#define ENOMEM 12  /* Out of memory */
#define EACCES 13  /* Permission denied */
#define EFAULT 14  /* Bad memory address */
#define EBUSY 16   /* Device or resource busy */
#define EEXIST 17  /* File exists */
#define ENODEV 19  /* No such device */
#define ENOTDIR 20 /* Not a directory */
#define EISDIR 21  /* Is a directory */
#define EINVAL 22  /* Invalid argument */
#define ENFILE 23  /* Too many open files in system */
#define EMFILE 24  /* Too many open files */
#define ENOTTY 25  /* Inappropriate ioctl for device */
#define ENOSPC 28  /* No space left on device */
#define ESPIPE 29  /* Illegal seek */
#define EROFS 30   /* Read-only filesystem */
#define EMLINK 31  /* Too many links */
#define EPIPE 32   /* Broken pipe */
#define EDOM 33    /* Math argument out of domain */
#define ERANGE 34  /* Math result out of range */
#define ENOSYS 38  /* Function not implemented */
#define ETIMEDOUT 110 /* Connection timed out */
#define EADDRINUSE 98 /* Address already in use */
#define EAFNOSUPPORT 97 /* Address family not supported by protocol */
#define ECONNREFUSED 111 /* Connection refused */

#endif /* __SYS__ERR_NO_H__ */