#ifndef __SYS__IOFORGE__IOFORGE_PCI_HPP_
#define __SYS__IOFORGE__IOFORGE_PCI_HPP_

#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_pci.h"

class IOforgePCI : public IOForge {
      public:
	inline IOforgePCI(const char* mod) : IOForge(mod) {
	}
	inline struct ioforge_pci_device*
	findDevice(uint16_t vendor_id, uint16_t device_id) {
		return ioforge_find_pci_device(ioforge_get_pci_root(),
					       vendor_id, device_id);
	}
	virtual void load() = 0;
	virtual void unload() = 0;
};

#endif // __SYS__IOFORGE__IOFORGE_PCI_HPP_
