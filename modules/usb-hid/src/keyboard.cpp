#include "dev/event.h"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_int_pipe.hpp"
#include "type.h"
#include "vfs/dev.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <str.h>
#include <usb-hid/keyboard.hpp>
#include <vfs/cache.h>
#include <vfs/dentry.h>

static HIDKeyboard* instance = 0;

HIDKeyboard::HIDKeyboard() { instance = this; }

void HIDKeyboard::init_vfs() {}

void HIDKeyboard::load(ioforge_usb_device* dev) {
	volatile ioforge_usb_device* dev_ = dev;

	serial2_printf("dev before vxnamei2: %p %p\n", dev_, dev);
	serial2_printf("addr of dev_: %p\n",
	               (void*)&dev_); // lihat di mana dev_ di stack

	dentry_ptr dentry_;
	vxnamei("/dev/event/event0", &dentry_);

	serial2_printf("dev before vxnamei2: %p %p\n", dev_, dev);

	auto priv =
	    (struct dev_event_data*)kalloc(sizeof(struct dev_event_data));
	if (!priv) {
		serial2_printf("failed alloc priv\n");
		return;
	}
	serial2_printf("private alloc 0x%x\n", priv);
	memset(priv, 0, sizeof(struct dev_event_data));

	priv->data = (uint8_t*)kalloc(128);

	inode_ = create_and_attach_vnode();
	if (!inode_) {
		serial2_printf("failed create inode\n");
		return;
	}
	serial2_printf("inode at 0x%x\n", inode_);
	dentry_->vnode = inode_;
	inode_->permission = 600;
	inode_->type = VNODE_TYPE_CHR;
	inode_->size = 128;

	inode_->vnode_private = (void*)priv;

	// setup usb interrupt pipe
	USBInterruptPipe* pipe = (USBInterruptPipe*)dev_->pipe;

	if (priv == (void*)pipe) {
		serial2_printf("FATAL: priv aliasing pipe!\n");
		return;
	}

	auto desc = (struct USBInterruptPipeDesc){
	    .dev_addr = dev_->addr,
	    .endpoint = (uint8_t)(dev_->endpoints[0].address & 0xF),
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

	auto event = (struct dev_event_data*)inode_->vnode_private;
	event->len = len;
	event->available = len > 0;
	memcopy((void*)event->data, (void*)data, len);

	// for (int i = 0; i < (int)len; i++) {
	// 	serial2_printf("%x ", data[i]);
	// }
	// serial2_printf("\n");
}

void HIDKeyboard::fireHandler(const uint8_t* data, size_t len) {
	if (instance)
		instance->store_in_vfs(data, len);
}