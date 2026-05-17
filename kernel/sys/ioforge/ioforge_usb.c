
#include "ioforge/ioforge_usb.h"
#include "ioforge/ioforge.h"
#include "libk/serial.h"
#include "memory/kalloc.h"
#include "memory/memory_utils.h"
#include <type.h>

static bool ioforge_can_contain_usb_device(IoForgeType type) {
	switch (type) {
	case IOFORGE_ROOT:
	case IOFORGE_USB_DEVICE:
		return true;
	default:
		return false;
	}
}

void KERNEL_API ioforge_find_usb_device_by_devclass(
	struct ioforge_device* root, uint16_t devclass,
	ioforge_usb_visitor_fn callback, void* ctx) {

	if (!root)
		return;

#define MAX_USB_NODES 64
	struct ioforge_device** stack =
		(struct ioforge_device**) kalloc(sizeof(void*) * MAX_USB_NODES);
	if (!stack)
		return;

	int top = 0;
	stack[top++] = root;

	while (top > 0) {
		struct ioforge_device* node = stack[--top];
		if (!node)
			continue;
		if (!ioforge_can_contain_usb_device(node->type))
			continue;

		if (node->type == IOFORGE_USB_DEVICE) {
			struct ioforge_usb_device* usb =
				(struct ioforge_usb_device*) node;
			if (usb->class_code == devclass) {
				callback(usb, ctx);
			}
		}

		struct ioforge_device* child = node->first_child;
		while (child) {
			if (top >= MAX_USB_NODES) {
				// log warning: node truncated
				serial_printf("[ioforge] WARNING: USB "
					      "traversal stack full, "
					      "some devices may be skipped\n");
				break;
			}
			stack[top++] = child;
			child = child->next_sibling;
		}
	}

	kfree(stack, sizeof(void*) * MAX_USB_NODES);
}