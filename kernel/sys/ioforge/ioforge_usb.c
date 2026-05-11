
#include "ioforge/ioforge_usb.h"
#include "ioforge/ioforge.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include <type.h>

static bool ioforge_can_contain_usb_device(IoForgeType type) {
	switch (type) {
	case IOFORGE_ROOT:
	case IOFORGE_USB_DEVICE:
		return true;
	default:
		return false; /* USB, NIC, UART, dll → skip */
	}
}

void KERNEL_API ioforge_find_usb_device_by_devclass(
	struct ioforge_device* node, uint16_t devclass,
	ioforge_usb_visitor_fn callback, void* ctx) {

	if (!node)
		return;

	if (!ioforge_can_contain_usb_device(node->type))
		return;

	struct ioforge_usb_device* usb = (struct ioforge_usb_device*) node;
	if (usb->class_code == devclass) {
		callback(usb, ctx);
	}

	struct ioforge_device* child = node->first_child;
	while (child) {
		ioforge_find_usb_device_by_devclass(child, devclass, callback,
						    ctx);
		child = child->next_sibling;
	}
}