#ifndef __USB_AHCI__AHCI_HPP__
#define __USB_AHCI__AHCI_HPP__

#include "ahci/ahci_reg.hpp"
#include "ioforge/ioforge_pci.hpp"
#include <stdint.h>

class AHCIModule : public IOforgePCI
{
  public:
    AHCIModule();
    void      load() override;
    void      unload() override;
    bool      read(uint16_t port, uint32_t startl, uint32_t starth, uint32_t count, uint16_t *buf);
    boolean_t isDevicePresent(uint16_t port);
    ahci_device_type_t getDeviceType(uint16_t port);

  protected:
    void setup();
    class ATAPI
    {
      public:
        static bool testUnitReady(ahci_op_t *op, uint16_t port);
    };

  private:
    ioforge_pci_service *device;
    ahci_op_t           *op;
};

#endif //__USB_AHCI__AHCI_HPP__