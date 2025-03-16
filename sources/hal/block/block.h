#ifndef __HAL__BLOCK__BLOCK_H__
#define __HAL__BLOCK__BLOCK_H__

#include <libk/type.h>

typedef struct
{
    void *ctx;
    uint8_t *(*read) (void *this, uint64_t offset, size_t count);
    int (*write) (void *this, uint64_t offset, size_t count, uint8_t *data);
} block_device_operations_t;

struct block_device
{
    bool used;
    void *identifier;
    block_device_operations_t *ops;
    const char *name;
};

void block_install ();
void block_register_device (const char *name, block_device_operations_t *ops,
                            void *identifier);
struct block_device *block_get_device (const char *name);

#define BLOCK_OPT_NOT_IMPLEMENTED -1

#endif // __HAL__BLOCK__BLOCK_H__