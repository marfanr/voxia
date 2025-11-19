#ifndef __SYS__IOFORGE__IOFORGE_NIC_H__
#define __SYS__IOFORGE__IOFORGE_NIC_H__

#include "ioforge/ioforge.h"
#include "type.h"

struct ioforge_nic_operation
{
    int (*send)(const void *data, size_t len);
    int (*receive)(void **buffer, size_t *size);
    int (*get_mac_address)(uint8_t mac[6]);
};

struct ioforge_nic_service
{
    struct ioforge_service        service;
    struct ioforge_nic_operation *ops;
    uint8_t                       mac[6];
};

#ifdef __cplusplus
extern "C"
{
#endif

    void                        IOforgeRegisterNIC(struct ioforge_nic_service *nic);
    struct ioforge_nic_service *IOforgeNICFindByName(char *name);

#ifdef __cplusplus
}
#endif
#endif // __SYS__IOFORGE__IOFORGE_NIC_H__