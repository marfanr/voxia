#include "descriptor.h"
#include "init/init.h"
#include "vfs/dentry.h"

#include <libk/serial.h>
#include <libk/str.h>
#include <libk/vector.h>
#include <memory/slab.h>

static struct slab_cache* descriptor_cache = 0;

#define MAX_FD_NUMBER 512

INIT(descriptor) {
	vxCreateSlabCache(&descriptor_cache, "descriptor",
	                  sizeof(file_descriptor_t), 64, 0);
}
