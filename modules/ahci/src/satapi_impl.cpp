#include "ahci/block_impl.hpp"
#include "ioforge/ioforge.hpp"

block_device_operations_t *
satapi_ops_impl(int port)
{
    block_device_operations_t *ops =
        (block_device_operations_t *)IOForge::IOUtils::alloc(sizeof(block_device_operations_t));
    auto satapi_read_impl = +[](uint64_t pos, size_t size) -> uint8_t * { return 0; };
    ops->read             = satapi_read_impl;
    return ops;
}