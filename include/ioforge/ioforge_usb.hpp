#ifndef __IOFORGE__IOFORGE_USB_HPP_
#define __IOFORGE__IOFORGE_USB_HPP_

#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_usb.h"
#include <usb.h>

class IoForgeUSB : public IOForge {
      public:
	IoForgeUSB(const char* mod) : IOForge(mod) {
	}
	virtual void load();
	virtual void unload();

	inline static void send(uint32_t data, size_t size) {
	}

	template <typename Fn>
	inline static void
	foreach_usb_device_by_devclass(struct ioforge_device* node,
				       uint16_t devclass, Fn&& fn) {
		// Bungkus lambda ke dalam void* ctx
		ioforge_find_usb_device_by_devclass(
			node, devclass,
			[](struct ioforge_usb_device* dev, void* ctx) {
				(*reinterpret_cast<Fn*>(ctx))(dev);
			},
			reinterpret_cast<void*>(&fn));
	};

	inline static uint8_t
	get_configuration(struct ioforge_usb_device* dev) {
		uintptr_t setup_addr = 0;
		struct usb_setup_packet* setup =
			(struct usb_setup_packet*) IOUtils::DMAAlloc(
				sizeof(struct usb_setup_packet), &setup_addr);

		uintptr_t resp_paddr = 0;
		uint8_t* resp = (uint8_t*) IOUtils::DMAAlloc(64, &resp_paddr);
		IOUtils::memset(resp, 0, 64);

		setup->bRequest = 0x08;
		setup->bmRequestType = 0b10000000;
		setup->wValue = 0;
		setup->wIndex = 0;
		setup->wLength = 1;

		// todo: modify send method to add endpoint parameter
		dev->controller->ops.send(dev->addr, 0, setup_addr,
					  sizeof(*setup), resp_paddr, 1);

		uint8_t res = *resp;

		IOUtils::DMAFree((void*) setup_addr, (void*) setup,
				 sizeof(*setup));
		IOUtils::DMAFree((void*) resp_paddr, (void*) resp,
				 sizeof(*resp));

		return res;
	}

	inline static void
	set_configuration(struct ioforge_usb_device* dev, uint8_t val) {
		uintptr_t setup_addr = 0;
		struct usb_setup_packet* setup =
			(struct usb_setup_packet*) IOUtils::DMAAlloc(
				sizeof(struct usb_setup_packet), &setup_addr);

		setup->bmRequestType = 0x0;
		setup->bRequest = 0x09;
		setup->wValue = val;
		setup->wIndex = 0;
		setup->wLength = 0;

		// todo: modify send method to add endpoint parameter
		dev->controller->ops.send(dev->addr, 0, setup_addr,
					  sizeof(struct usb_setup_packet), 0,
					  0);
		IOUtils::DMAFree((void*) setup_addr, (void*) setup,
				 sizeof(*setup));
	}
};

#endif // __IOFORGE__IOFORGE_USB_HPP_