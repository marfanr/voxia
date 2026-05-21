#ifndef __INIT__LOADER_H__
#define __INIT__LOADER_H__

#include "init/init.h"
#include <libk/stivale2.h>

// static struct stivale2_tag                    l5_tag;
// static struct stivale2_header_tag_framebuffer framebuffer_hdr_tag;

void *stivale2_get_tag(struct stivale2_struct *stivale2_struct, uint64_t id);
void  build_context_from_stivale2(struct stivale2_struct *stivale2_struct, init_context_t *ctx);
void  run_all_init_calls(init_context_t *ctx);

#endif // __INIT__LOADER_H__
