#ifndef __HAL__BLOCK__BLOCK_H__
#define __HAL__BLOCK__BLOCK_H__

#include "block/block.h"

typedef struct block_device block_device;
struct block_device
{
    uintptr_t                  bar;
    boolean_t                  used;
    void                      *identifier;
    block_device_operations_t *ops;
    const char                 name[64];
    struct block_device       *next;
} __attribute__((aligned(64)));

void block_install();
void block_register_device(const char *name, block_device_operations_t *ops, void *identifier);
block_device *block_get_device(const char *name);

#define BLOCK_OPT_NOT_IMPLEMENTED -1

#endif // __HAL__BLOCK__BLOCK_H__
