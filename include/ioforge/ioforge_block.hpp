#ifndef __IOFORGE__IOFORGE_BLOCK_HPP__
#define __IOFORGE__IOFORGE_BLOCK_HPP__

#include "block/block.h"

class IOForgeBlock
{
  public:
    static void create(const char *name, block_device_operations_t *ops, void *identifier);
};

#endif // __IOFORGE__IOFORGE_BLOCK_HPP__