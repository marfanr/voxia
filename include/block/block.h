#ifndef __BLOCK__BLOCK_H__
#define __BLOCK__BLOCK_H__

#include "type.h"
#ifdef __cplusplus
extern "C"
{
#endif
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
        int (*write)(void *block, uint64_t offset, size_t count, uint8_t *data);
    } __attribute__((aligned(64))) block_device_operations_t;

    void IOforgeRegisterBlockDevice(const char *name, block_device_operations_t *ops,
                                    void *identifier);
#ifdef __cplusplus
}
#endif

#endif // __BLOCK__BLOCK_H__