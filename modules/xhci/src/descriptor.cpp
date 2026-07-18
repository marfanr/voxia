#include "memory/kalloc.h"
#include <xhci/xhci.hpp>
#include <usb.h>

/* Descriptor helpers */
void XHCIModule::usb_get_descriptor(uint8_t addr, uint8_t type, uint8_t index,
                                    uint8_t len, uint8_t* data) {
	// Setup packet + data buffer in one 64-byte-aligned DMA block
	size_t total = sizeof(struct usb_setup_packet) + len;
	auto alloc = dma_alloc_aligned(total, 64);

	auto* cmd = (struct usb_setup_packet*)alloc.vaddr;
	cmd->bRequest = 0x06;
	cmd->bmRequestType = 0x80;
	cmd->wValue = (uint16_t)((type << 8) | index);
	cmd->wIndex = 0;
	cmd->wLength = len;

	uintptr_t data_phys = alloc.paddr + sizeof(usb_setup_packet);
	uint8_t* data_buf = (uint8_t*)alloc.vaddr + sizeof(usb_setup_packet);

	send_async_with_response(addr, 0, *(uint64_t*)cmd,
	                         sizeof(usb_setup_packet), data_phys, len);

	IOUtils::memcpy(data, data_buf, len);
	IOUtils::DMAFree((void*)alloc.raw_paddr, alloc.raw_vaddr,
	                 alloc.raw_size);
}

void XHCIModule::usb_get_string_descriptor(uint8_t addr, uint8_t index,
                                           char* data, size_t size) {
	uint8_t* buffer = (uint8_t*)kalloc(255);
	IOUtils::memset(buffer, 0, 255);
	usb_get_descriptor(addr, 0x3, index, 255, buffer);

	auto* str = (struct usb_string_descriptor*)buffer;
	if (str->bLength < 2) {
		data[0] = 0;
		IOUtils::free(buffer, 255);
		return;
	}

	size_t len = (str->bLength - 2) / 2;
	size_t j = 0;
	for (size_t i = 0; i < len && j < size - 1; i++) {
		uint16_t ch = str->wData[i];
		if (ch < 128)
			data[j++] = (char)ch;
	}
	data[j] = 0;
	IOUtils::free(buffer, 255);
}