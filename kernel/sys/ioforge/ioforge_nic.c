#include "ioforge/ioforge_nic.h"
#include "ioforge/ioforge.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "libk/type.h"

extern struct ioforge_service *ioforge_services;

KERNEL_API void
IOforgeRegisterNIC(struct ioforge_nic_service *nic)
{
    nic->service.type = IOFORGE_NIC;
    LOG_DEBUG("NIC", "registered NIC at 0x%x", nic);
    ioforge_register_service((struct ioforge_service *)nic);
}

KERNEL_API
struct ioforge_nic_service *
IOforgeNICFindByName(char *name)
{
    struct ioforge_service *tmp = ioforge_services;
    while (tmp != 0)
    {
        if (tmp->type == IOFORGE_NIC)
        {
            LOG_INFO("NIC", "found %s", tmp->name);
            struct ioforge_nic_service *tmp_nic = (struct ioforge_nic_service *)tmp;
            if (strncmp(tmp_nic->service.name, name, strlen(name)))
            {
                return tmp_nic;
            }
        }
        tmp = tmp->next;
    }
    return 0;
}