#ifndef __USB_HID__HID_HPP__
#define __USB_HID__HID_HPP__

#include "ioforge/ioforge_nic.h"
#include "ioforge/ioforge_pci.hpp"
#include "type.h"

#define E1000_NUM_RX_DESC 256
#define E1000_NUM_RX_MASK (E1000_NUM_RX_DESC - 1)
#define E1000_NUM_TX_DESC 64
#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100c

struct e1000_rx_desc {
	volatile uint64_t addr;
	volatile uint16_t length;
	volatile uint16_t checksum;
	volatile uint8_t status;
	volatile uint8_t errors;
	volatile uint16_t special;
} __attribute__((packed));

struct e1000_rx_comp {
	volatile uint64_t addr;
	volatile uint64_t paddr;
};

struct e1000_tx_desc {
	volatile uint64_t addr;
	volatile uint16_t length;
	volatile uint8_t cso;
	volatile uint8_t cmd;
	volatile uint8_t status;
	volatile uint8_t css;
	volatile uint16_t special;
} __attribute__((packed));

class E1000Module : public IOforgePCI {
      public:
	static E1000Module* getInstance();
	E1000Module();
	void load() override;
	void unload() override;
	int sendPacket(const struct data_template data[], size_t count);
	int receivePacket(void** buffer, size_t* size);
	inline void setNIC(struct ioforge_nic_service* nic) {
		this->nic = nic;
	}

	boolean_t detectEeprom();
	uint32_t readEeprom(uint32_t addr);
	boolean_t syncMacAddress();
	void enableInterrupt();
	void disableInterrupt();
	static void fireHandler();
	void linkup();
	void write(uint16_t p_address, uint32_t p_value);
	void receiveHandle();
	uint32_t read(uint16_t p_address);
	int getMacAddress(uint8_t mac[6]);
	void storeBufferToPool(int rx_id, void* vaddr);

      private:
	bool mac_ready = false;
	uint32_t mac_addr[6];
	ioforge_pci_service* device;
	boolean_t eerprom_exists = false;
	void initReceiverX();
	void initTransmitterX();

	uint32_t rx_cur;
	uint32_t tx_cur;

	// receive packet
	bool got_packet = false;
	int last_readed_rx_cur = 0;
	ioforge_nic_service* nic = 0;
};

#endif //__USB_HID__HID_HPP__