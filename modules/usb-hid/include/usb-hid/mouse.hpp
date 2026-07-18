#ifndef __USB_HID__MOUSE_HPP__
#define __USB_HID__MOUSE_HPP__

#include "ioforge/ioforge_usb.h"
#include <type.h>

class HIDMouse {
      public:
	HIDMouse();
	static void fireHandler(const uint8_t* data, size_t len);
	void load(ioforge_usb_device* dev);

      private:
	void parse_report(const uint8_t* data, size_t len);
	ioforge_usb_device* dev_ = nullptr;
};

#endif // __USB_HID__MOUSE_HPP__
