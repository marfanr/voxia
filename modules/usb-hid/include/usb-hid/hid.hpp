#ifndef __USB_HID__HID_HPP__
#define __USB_HID__HID_HPP__

#include "ioforge/ioforge_usb.hpp"

enum hid_protocol { BOOT_PROTOCOL = 0, REPORT_PROTOCOL = 1 };

class HIDModule : public IoForgeUSB {
      public:
	HIDModule();
	void load() override;
	void unload() override;
	static HIDModule* getInstance();
	static void fireHandler();

      protected:
	void hid_device_setup(ioforge_usb_device* dev);
	void set_iddle(ioforge_usb_device* dev);
	void set_report(ioforge_usb_device* dev, uint8_t report);
	void get_report(ioforge_usb_device* dev);
	void set_protocol(ioforge_usb_device* dev, uint8_t interface,
			  uint8_t protocol);

	static void callback(const uint8_t* data, size_t len);
};

#endif //__USB_HID__HID_HPP__