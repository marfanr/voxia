#ifndef __SYS__IOFORGE__IOFORGE_PCI_HPP_
#define __SYS__IOFORGE__IOFORGE_PCI_HPP_

#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_pci.h"

class IOforgePCI : public IOForge
{
  public:
    IOforgePCI(const char *mod);
    struct ioforge_pci_service *findDevice(uint16_t vendor_id, uint16_t device_id);
    virtual void                load()   = 0;
    virtual void                unload() = 0;
};

#endif // __SYS__IOFORGE__IOFORGE_PCI_HPP_
