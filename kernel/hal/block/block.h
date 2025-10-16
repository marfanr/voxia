#ifndef __HAL__BLOCK__BLOCK_H__
#define __HAL__BLOCK__BLOCK_H__

#include <libk/type.h>

typedef enum
{
    FMODE_R = 1UL << 0,
    FMODE_W = 1UL << 1,
    FMODE_E = 1UL << 2
} fmode_t;

typedef enum
{
    OPEN_SUCCESS = 0,
    OPEN_FAILED  = 1
} open_response_code;

typedef struct
{
    open_response_code (*open)(fmode_t mode);
    int (*close)(void);
    uint8_t *(*read)(uint64_t offset, size_t count);
    int (*write)(void *this, uint64_t offset, size_t count, uint8_t *data);
} __attribute__((aligned(64))) block_device_operations_t;

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
