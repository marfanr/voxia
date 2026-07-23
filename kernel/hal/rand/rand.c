#include "hal/rand/rand.h"
#include "hal/acpi/hpet.h"
#include "hal/cpu/cpuid.h"
#include "init/init.h"
#include "libk/serial.h"
#include "str.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"

#define RAND_MAX 0xFFFFFFFF

static boolean_t random_available = 0;
static dentry_ptr urandom_dentry = 0;

static int urandom_read(vnode_t* vnode, void* buf, size_t len, size_t offset)
{
	(void)vnode;
	(void)offset;
	(void)buf;
	(void)len;

	uint32_t random = vxRand();
	memcopy(buf, &random, sizeof(uint32_t));
	return 0;
}

static vops_file_t urandom_ops = {
	.read = urandom_read,
};

INIT(Rand) {
	uint32_t ecx, unused;
	cpuid(1, 0, &unused, &unused, &ecx, &unused);
	random_available = (ecx >> 30) & 1;
	LOG_INFO("Rand ", "random is available %d", random_available);
	if (vxnamei("/dev/urandom", &urandom_dentry) != VFS_OK) {
		return;
	}

	urandom_dentry->vnode = create_and_attach_vnode();
	urandom_dentry->vnode->type = VNODE_TYPE_FILE;
	urandom_dentry->vnode->permission = 0444;
	urandom_dentry->vnode->ops = &urandom_ops;
}

uint32_t vxRand() {
	if (random_available) {
		uint32_t rand = 0;
		asm volatile("rdrand %0" : "=r"(rand));
		return rand & RAND_MAX;
	} else if (vxHPETIsAvailable()) {
		// fallback using HPET if available
		uint32_t rand = vxHPETGetMainCount() & RAND_MAX;
		return rand;
	}
	return 0;
}