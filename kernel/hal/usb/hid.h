#ifndef __HAL__USB__HID_H__
#define __HAL__USB__HID_H__

#include <type.h>

enum USB_HID_REPORT {
	USB_HID_REPORT_TYPE_KEYBOARD = 1,
	USB_HID_REPORT_TYPE_MOUSE = 2,
};

struct usb_hid_report {
	uint8_t id;
	uint8_t type;
	uint8_t size;
	uint8_t* data;
};

struct usb_hid_report*
usb_hid_parse_report_descriptor(uint8_t* desc, uint32_t len);
void usb_hid_initalize();

#endif // __HAL__USB__HID_H__