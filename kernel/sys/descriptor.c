#include "descriptor.h"
#include "init/init.h"
#include "vfs/dentry.h"

#include <vector.h>
#include <libk/serial.h>
#include <str.h>
#include <memory/slab.h>

static struct slab_cache* descriptor_cache = 0;

#define MAX_FD_NUMBER 512

INIT(descriptor) {
	serial_trace("descriptor init\n");
	vxCreateSlabCache(&descriptor_cache, "descriptor",
			  sizeof(file_descriptor_t), 64, 0);
}
