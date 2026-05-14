#ifndef __USB_AHCI__AHCI_HPP__
#define __USB_AHCI__AHCI_HPP__

#include "ahci/ahci_reg.hpp"
#include "ioforge/ioforge_pci.h"
#include "ioforge/ioforge_pci.hpp"
#include "type.h"

struct ahci_internal_vaddr {
	uintptr_t clb;
	uintptr_t fb;
	uintptr_t cmd[32];
};

class AHCIModule : public IOforgePCI {
      public:
	AHCIModule();
	void load() override;
	void unload() override;
	static AHCIModule* getInstance();
	int submit_impl(struct ioforge_block_device* dev,
			struct ioforge_block_request* req);

      protected:
	void port_power_off(ahci_port_t* port);
	void port_power_on(ahci_port_t* port);
	void port_reset(ahci_port_t* port);
	void setup();
	void probe();
	void
	port_configure(ahci_port_t* port, struct ahci_internal_vaddr* vaddr);

	bool ata_rw(ahci_port_t* p, struct ahci_internal_vaddr* vaddr,
		    struct ioforge_block_request* req);
	int atapi_packet(ahci_port_t* p, struct ahci_internal_vaddr* vaddr,
			 struct ioforge_block_request* req);
	int ata_identify(ahci_port_t* p, struct ahci_internal_vaddr* vaddr,
			 struct ioforge_block_request* req);

	int ata_flush(ahci_port_t* p, struct ahci_internal_vaddr* vaddr,
		      struct ioforge_block_request* req);

	boolean_t issue_and_wait(ahci_port_t* p, int slot, uint32_t timeout);

      private:
	struct ioforge_pci_device* dev_;
	ahci_op_t* op;
};

#endif //__USB_AHCI__AHCI_HPP__