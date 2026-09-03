#include "input.h"
#include "ioforge/ioforge_int_pipe.hpp"
#include <stdint.h>
#include <usb-hid/mouse.hpp>

static HIDMouse* instance = 0;

HIDMouse::HIDMouse() { instance = this; }

void HIDMouse::load(ioforge_usb_device* dev) {
	dev_ = dev;

	USBInterruptPipe* pipe = (USBInterruptPipe*)dev_->pipe;

	uint8_t b = dev_->endpoints[0].interval;
	uint16_t interval_ms = b; 
	if (interval_ms < 1)
		interval_ms = 1;

	auto desc = (struct USBInterruptPipeDesc){
	    .dev_addr = dev_->addr,
	    .endpoint = (uint8_t)(dev_->endpoints[0].address & 0xF),
	    .speed = 2,
	    .interval_ms = b,
	    .buffer_size = 8,
	};
	pipe->open(desc, HIDMouse::fireHandler);
}

void HIDMouse::parse_report(const uint8_t* data, size_t len) {
	if (len < 3) return;

	uint8_t buttons = data[0];
	int16_t x = (int8_t)data[1]; // Typically signed 8-bit relative movement
	int16_t y = (int8_t)data[2]; // Typically signed 8-bit relative movement
	int8_t z = 0;

	if (len >= 4) {
		z = (int8_t)data[3];
	}

	input_report_mouse(&instance->dev_->controller->service, x, y, z, buttons);
}

void HIDMouse::fireHandler(const uint8_t* data, size_t len) {
	if (!instance) {
		return;
	}
	instance->parse_report(data, len);
}
