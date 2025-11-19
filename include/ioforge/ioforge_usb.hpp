#ifndef __IOFORGE__IOFORGE_USB_HPP_
#define __IOFORGE__IOFORGE_USB_HPP_

#include "ioforge/ioforge.hpp"
#include <cstdint>

enum IoForgeUSB_VERSION
{
    IoForgeUSB_VERSION_2 = 2
};

class IoForgeUSB : public IOForge
{
  public:
    IoForgeUSB(const char *mod);
    virtual void load();
    virtual void unload();

    void send(uint32_t data, size_t size);
};

// struct IoForgeUSB
// {
//     struct IoForgeService service;
//     uint8_t version;
//     uint8_t addr;
//     uint8_t endpoint;
//     uint8_t length;
//     void *data;
//     uint8_t interrupt;
// };

#endif // __IOFORGE__IOFORGE_USB_HPP_