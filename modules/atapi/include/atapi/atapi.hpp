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
	void probe(struct ioforge_block_device* block);
};

#endif //__USB_HID__HID_HPP__