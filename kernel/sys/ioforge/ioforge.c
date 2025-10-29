#include "./ioforge.h"
#include "./ioforge_pci.h"
#include "init/init.h"
#include "libk/type.h"
#include <hal/pci/pci.h>
#include <libk/serial.h>

static struct IoForgeService *services;

void
ioforge_init(init_context_t *_)
{
    // enumerate pci
    pci_scan();
}
// REGISTER_INIT(ioforge)

KERNEL_API
void
ioforge_register_service(struct IoForgeService *service)
{
    if (service->init != 0)
    {
        service->init(service);
    }
    if (services == 0)
    {
        services = service;
        return;
    }

    serial_trace("ioforge register ok 0x%x\n", service);
    struct IoForgeService *tmp = services;
    while (tmp->next != 0)
    {
        tmp = tmp->next;
    }
    tmp->next = service;
}

KERNEL_API
struct IoForgePCI *
ioforge_get_pci_device(uint16_t vendor_id, uint16_t device_id)
{
    LOG_INFO("IOFORGE", "looking for pci device 0x%x", vendor_id);
    struct IoForgePCI     *pci = 0;
    struct IoForgeService *tmp = services;
    while (tmp != 0)
    {
        if (tmp->type == IOFORGE_PCI)
        {
            struct IoForgePCI *tmp_pci = (struct IoForgePCI *)tmp;
            if (tmp_pci->vendor_id == vendor_id && tmp_pci->device_id == device_id)
            {
                LOG_INFO("IOFORGE", "found pci device 0x%x", tmp_pci);
                pci = tmp_pci;
                break;
            }
        }
        tmp = tmp->next;
    }
    return pci;
}
