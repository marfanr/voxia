#ifndef __USB_AHCI__AHCI_HPP__
#define __USB_AHCI__AHCI_HPP__

#include "ahci/ahci_reg.hpp"
#include "ioforge/ioforge_pci.h"
#include "ioforge/ioforge_pci.hpp"

class AHCIModule : public IOforgePCI {
      public:
	AHCIModule();
	void load() override;
	void unload() override;
	static AHCIModule* getInstance();

	// bool read(uint16_t port, uint32_t startl, uint32_t starth,
	// 	  uint32_t count, uint16_t* buf);
	// boolean_t isDevicePresent(uint16_t port);
	// ahci_device_type_t getDeviceType(uint16_t port);

      protected:
	void setup();
	void port_power_off(ahci_port_t* port);
	void port_power_on(ahci_port_t* port);
	void port_reset(ahci_port_t* port);

	// class ATAPI {
	//       public:
	// 	static bool testUnitReady(ahci_op_t* op, uint16_t port);
	// };

      private:
	struct ioforge_pci_device* dev_;
	ahci_op_t* op;
};

#endif //__USB_AHCI__AHCI_HPP__