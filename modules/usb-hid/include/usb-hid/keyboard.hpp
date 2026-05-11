#ifndef __USB_HID__KEYBOARD_HPP__
#define __USB_HID__KEYBOARD_HPP__

#include "ioforge/ioforge_usb.h"
#include "vfs/dentry.h"
#include <type.h>

class HIDKeyboard {
      public:
	HIDKeyboard(ioforge_usb_device* dev);
	static void fireHandler(const uint8_t* data, size_t len);
	void store_in_vfs(const uint8_t* data, size_t len);

      private:
	ioforge_usb_device* dev_ = 0;
	vnode_ptr_t inode_ = 0;
	dentry_ptr dentry_ = 0;
};

#endif // __USB_HID__KEYBOARD_HPP__