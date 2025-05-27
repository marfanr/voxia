#include "slab.h"
#include <libk/serial.h>
#include <libk/str.h>

void
slab_cache_create (struct slab_cache *cache, const char *name, size_t obj_size,
                   size_t alignment)
{
    memset (cache, 0, sizeof (struct slab_cache));
    // cache->
}