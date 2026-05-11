#ifndef __EHCI__EHCI_HPP__
#define __EHCI__EHCI_HPP__

#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_virtio.hpp"

class HIDModule : public IoForgeVirtio
{
  public:
    HIDModule();
    void load() override;
    void unload() override;
};

#endif //__EHCI__EHCI_HPP__