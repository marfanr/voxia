#ifndef __INIT__LOADER_H__
#define __INIT__LOADER_H__

#include <libk/stivale2.h>

static struct stivale2_tag l5_tag;
static struct stivale2_header_tag_framebuffer framebuffer_hdr_tag;

void *stivale2_get_tag(struct stivale2_struct *stivale2_struct, uint64_t id);

#endif // __INIT__LOADER_H__
