#ifndef __INIT__LOADER_H__
#define __INIT__LOADER_H__

#include "init/init.h"

void build_context_from_limine(init_context_t *ctx);
void  run_all_init_calls(init_context_t *ctx);

#endif // __INIT__LOADER_H__
