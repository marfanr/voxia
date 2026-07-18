#ifndef _LINUX_INPUT_H
#define _LINUX_INPUT_H

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

struct input_event {
	uint64_t tv_sec;
	uint64_t tv_usec;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

#define SYN_REPORT 0
#define SYN_CONFIG 1
#define SYN_MT_REPORT 2
#define SYN_DROPPED 3

#define REL_X 0x00
#define REL_Y 0x01
#define REL_Z 0x02
#define REL_WHEEL 0x08

#define BTN_MOUSE 0x110
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114

/*
 * IOCTLs
 */
#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS 2

#define _IOC_NRMASK ((1U << _IOC_NRBITS) - 1)
#define _IOC_TYPEMASK ((1U << _IOC_TYPEBITS) - 1)
#define _IOC_SIZEMASK ((1U << _IOC_SIZEBITS) - 1)
#define _IOC_DIRMASK ((1U << _IOC_DIRBITS) - 1)

#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT (_IOC_SIZESHIFT + _IOC_SIZEBITS)

#define _IOC_NONE 0U
#define _IOC_WRITE 1U
#define _IOC_READ 2U

#define _IOC(dir, type, nr, size)                                              \
	(((dir) << _IOC_DIRSHIFT) | ((type) << _IOC_TYPESHIFT) |               \
	 ((nr) << _IOC_NRSHIFT) | ((size) << _IOC_SIZESHIFT))

#define _IOR(type, nr, size) _IOC(_IOC_READ, (type), (nr), sizeof(size))

#define EVIOCGVERSION _IOR('E', 0x01, int)              /* get driver version */
#define EVIOCGID _IOR('E', 0x02, struct input_id)       /* get device ID */
#define EVIOCGNAME(len) _IOC(_IOC_READ, 'E', 0x06, len) /* get device name */
#define EVIOCGBIT(ev, len)                                                     \
	_IOC(_IOC_READ, 'E', 0x20 + (ev), len) /* get event bits */

struct input_id {
	uint16_t bustype;
	uint16_t vendor;
	uint16_t product;
	uint16_t version;
};

#ifdef __cplusplus
}
#endif

#endif
