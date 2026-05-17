#ifndef __USB_HID__HID_HPP__
#define __USB_HID__HID_HPP__

#include "ioforge/ioforge_block.h"
#include "ioforge/ioforge_block.hpp"

class ATAPIModule : public IOForgeBlock {
      public:
	ATAPIModule();
	void load() override;
	void unload() override;

	static ATAPIModule* getInstance();
	static int read(void* vdata, uintptr_t addr, void* buf, size_t count);
	static int write(void* vdata, uintptr_t addr, void* buf, size_t count);

      protected:
	void probe(struct ioforge_block_device* block);
	void identify(struct ioforge_block_device* block);
	void read_sector_size(struct ioforge_block_device* block);
	void build_acmd(uint8_t opcode, uint32_t lba, uint32_t sector_count,
			uint8_t (&acmd)[16]);

	void read_ascii(char* out, uint16_t off, uint16_t* buff, uint16_t len);

      private:
};

#endif //__USB_HID__HID_HPP__