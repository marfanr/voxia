#include "dev/event.h"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_int_pipe.hpp"
#include "memory/kalloc.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <vfs/dentry.h>
#include <usb-hid/keyboard.hpp>
#include <str.h>

static HIDKeyboard* instance = 0;

HIDKeyboard::HIDKeyboard(ioforge_usb_device* dev) : dev_(dev) {
	instance = this;

	vxNamei((char*) "/dev/event/event0", &dentry_);

	if (!dentry_) {
		serial2_printf("failed create dentry\n");
		return;
	}

	serial2_printf("dentry_ name %s\n", dentry_->name->c_str);
	inode_ = create_and_attach_vnode();
	dentry_->vnode = inode_;
	inode_->permission = 600;
	inode_->type = VNODE_TYPE_DIR;
	inode_->size = 128;
	inode_->vnode_private = (void*) kalloc(sizeof(struct dev_event_data));
	memset(inode_->vnode_private, 0, sizeof(struct dev_event_data));

	// setup usb interrupt pipe
	USBInterruptPipe* pipe = (USBInterruptPipe*) dev->pipe;
	serial2_printf("Pipe pointer before: 0x%x\n", pipe);

	auto desc = (struct USBInterruptPipeDesc){
		.dev_addr = dev->addr,
		.endpoint = (uint8_t) (dev->endpoints[0].address & 0xF),
		.speed = 2, // high speed
		.interval_ms = dev->endpoints[0].interval,
		.buffer_size = 128,
	};
	pipe->open(desc, HIDKeyboard::fireHandler);
}

void HIDKeyboard::store_in_vfs(const uint8_t* data, size_t len) {
	auto event = (struct dev_event_data*) inode_->vnode_private;
	event->data = data;
	event->len = len;
	event->available = len > 0;
}

void HIDKeyboard::fireHandler(const uint8_t* data, size_t len) {
	// for (size_t i = 0; i < len; i++)
	// 	serial2_printf("%x ", data[i]);

	// serial2_printf("\n");
	if (instance)
		instance->store_in_vfs(data, len);
}