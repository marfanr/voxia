#ifndef __SYS__IOFORGE__IOFORGE_USB_H_
#define __SYS__IOFORGE__IOFORGE_USB_H_

#include "ioforge.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ioforge_usb_version {
	IoForgeUSB_VERSION_2 = 0x20,
	IoForgeUSB_VERSION_3 = 0x30
};

struct UsbControllerOp {
	void (*send)(uint32_t addr, uint8_t endpoint, uint32_t data_phys,
		     size_t request_size, uint32_t response_phys,
		     size_t response_size);
	void (*put_into_periodic)(uint8_t addr, uint16_t ring, uint8_t endpoint,
				  uint32_t data_phys, size_t size,
				  uint32_t response, size_t response_size);
};

struct ioforge_usb_controller_service {
	struct ioforge_device service;
	struct UsbControllerOp ops;
};

struct ioforge_usb_endpoint {
	uint8_t address;     // bit[3:0]=nomor, bit[7]=1→IN / 0→OUT
	uint8_t attributes;  // bit[1:0]: 00=Control 01=Iso 10=Bulk 11=Interrupt
	uint16_t max_packet; // maksimal byte per transfer
	uint8_t interval;    // polling interval (ms, untuk Interrupt/Iso)
};

struct ioforge_usb_service {
	struct ioforge_device base;
	const char serial_number[64];

	// usb device descriptor
	uint16_t vendor_id;
	uint16_t product_id;
	uint8_t usb_version; // 0x10=USB1.1  0x20=USB2.0  0x30=USB3.0
	uint8_t class_code;
	uint8_t subclass_code;
	uint8_t protocol;
	uint8_t max_power;

	uint8_t addr;  // device address di bus (1–127)
	uint8_t port;  // port pada parent hub/controller
	uint8_t speed; // USB_SPEED_{LOW, FULL, HIGH, SUPER}
	uint8_t ep_count;

	struct ioforge_usb_controller_service* controller;
	struct ioforge_usb_endpoint endpoints[16];
};

struct ioforge_device* ioforge_get_usb_ctrl_root();
struct ioforge_device* ioforge_get_usb_devices_root();

typedef void (*ioforge_usb_visitor_fn)(struct ioforge_usb_service* dev,
				       void* ctx);

void ioforge_find_usb_device_by_devclass(struct ioforge_device* node,
					 uint16_t devclass,
					 ioforge_usb_visitor_fn callback,
					 void* ctx);
#ifdef __cplusplus
}
#endif

#endif // __SYS__IOFORGE__IOFORGE_PCI_H_