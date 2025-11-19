#ifndef __IOFORGE__IOFORGE_NIC_HPP__
#define __IOFORGE__IOFORGE_NIC_HPP__

#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.h"

class IoForgeNIC
{
  public:
    static void create(ioforge_nic_service *nic);
};

#endif // __IOFORGE__IOFORGE_NIC_HPP__