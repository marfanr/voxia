#ifndef __USB_H__
#define __USB_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

struct usb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
};

struct usb_config_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
} __attribute__((packed));

struct usb_interface {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
} __attribute__((packed));

struct usb_string_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wData[];
};

enum usb_request_type : uint8_t {
	GET_REPORT = 0x01,
	GET_IDLE = 0x02,
	GET_PROTOCOL = 0x03,
	SET_REPORT = 0x09,
	SET_IDLE = 0x0A,
	SET_PROTOCOL = 0x0B,
};

enum usb_report_type {
	REPORT_TYPE_INPUT = 1,
	REPORT_TYPE_OUTPUT = 2,
	REPORT_TYPE_FEATURE = 3
};

struct usb_setup_packet {
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};

enum usb_setup_packet_request {
	USB_SETUP_PACKET_GET_STATUS = 0,
	USB_SETUP_PACKET_CLEAR_FEATURE = 1,
	USB_SETUP_PACKET_SET_FEATURE = 3,
	USB_SETUP_PACKET_SET_ADDRESS = 5,
};

#ifdef __cplusplus
}
#endif

#endif // __USB_H__