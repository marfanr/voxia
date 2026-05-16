#include "dev/event.h"
#include "ioforge/ioforge.h"
#include <vfs/cache.h>
#include "ioforge/ioforge_int_pipe.hpp"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <vfs/dentry.h>
#include <usb-hid/keyboard.hpp>
#include <str.h>

static HIDKeyboard* instance = 0;

HIDKeyboard::HIDKeyboard() {
}

void HIDKeyboard::init_vfs() {
}

__attribute__((optnone)) void HIDKeyboard::load(ioforge_usb_device* dev) {
	const ioforge_usb_device* dev_ = dev;
	instance = this;

	serial2_printf("dev at : 0x%x\n", dev_);

	serial2_printf("before namei: this=%p dev=%p dev_=%p\n", this, dev,
		       dev_);

	dentry_ptr dentry_;
	vxnamei2("/dev/event/event0", &dentry_);

	if (!dentry_) {
		serial2_printf("failed create dentry\n");
		return;
	}

	void* priv = kalloc(sizeof(struct dev_event_data));
	if (!priv) {
		serial2_printf("failed alloc priv\n");
		return;
	}
	serial2_printf("private alloc 0x%x\n", priv);
	memset(priv, 0, sizeof(struct dev_event_data));

	serial2_printf("dentry_ name %s\n", dentry_->name->c_str);
	inode_ = create_and_attach_vnode();
	if (!inode_) {
		serial2_printf("failed create inode\n");
		return;
	}
	serial2_printf("inode at 0x%x\n", inode_);
	dentry_->vnode = inode_;
	inode_->permission = 600;
	inode_->type = VNODE_TYPE_DIR;
	inode_->size = 128;
	inode_->vnode_private = (void*) priv;

	// setup usb interrupt pipe
	serial2_printf("dev at : 0x%x\n", dev_);
	USBInterruptPipe* pipe = (USBInterruptPipe*) dev_->pipe;

	if (priv == (void*) pipe) {
		serial2_printf("FATAL: priv aliasing pipe!\n");
		return;
	}

	auto desc = (struct USBInterruptPipeDesc){
		.dev_addr = dev_->addr,
		.endpoint = (uint8_t) (dev_->endpoints[0].address & 0xF),
		.speed = 2, // high speed
		.interval_ms = dev_->endpoints[0].interval,
		.buffer_size = 128,
	};
	pipe->open(desc, HIDKeyboard::fireHandler);
}

void HIDKeyboard::store_in_vfs(const uint8_t* data, size_t len) {
	if (!inode_)
		return;

	if (!inode_->vnode_private)
		return;

	auto event = (struct dev_event_data*) inode_->vnode_private;
	event->len = len;
	event->available = len > 0;
}

void HIDKeyboard::fireHandler(const uint8_t* data, size_t len) {
	if (instance)
		instance->store_in_vfs(data, len);
}