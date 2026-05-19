#ifndef __USB_HID__HID_HPP__
#define __USB_HID__HID_HPP__

#include "ioforge/ioforge_usb.hpp"
#include "usb-hid/keyboard.hpp"

enum hid_protocol { BOOT_PROTOCOL = 0, REPORT_PROTOCOL = 1 };

enum hid_device { HID_KEYBOARD = 1, HID_MOUSE = 2 };

class UsbHid : public IoForgeUSB {
      public:
	UsbHid();
	void load() override;
	void unload() override;
	static UsbHid* getInstance();

      protected:
	void hid_device_setup(ioforge_usb_device* dev);
	void set_iddle(ioforge_usb_device* dev);
	void set_report(ioforge_usb_device* dev, uint8_t report);
	void get_report(ioforge_usb_device* dev);
	void set_protocol(ioforge_usb_device* dev, uint8_t interface,
			  uint8_t protocol);

      private:
	static HIDKeyboard keyboard;
};

#endif //__USB_HID__HID_HPP__