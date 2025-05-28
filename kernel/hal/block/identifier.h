#ifndef __HAL__BLOCK__IDENTIFIER_H__
#define __HAL__BLOCK__IDENTIFIER_H__

#include <libk/type.h>

typedef struct
{
    uint8_t port;
} block_identifier_usb_t;

typedef struct
{
    uint8_t port;
    uint8_t device;
    uint8_t *guid;
} block_identifier_ahci_t;

#endif // __HAL__BLOCK__IDENTIFIER_H__