#ifndef __USB_HID__KEYBOARD_HPP__
#define __USB_HID__KEYBOARD_HPP__

#include "ioforge/ioforge_usb.h"
#include "vfs/dentry.h"
#include <type.h>

class HIDKeyboard {
      public:
	HIDKeyboard();
	static void fireHandler(const uint8_t* data, size_t len);
	void store_in_vfs(const uint8_t* data, size_t len);
	void load(ioforge_usb_device* dev);

      private:
	void init_vfs();
	vnode_ptr_t inode_ = 0;
};

#endif // __USB_HID__KEYBOARD_HPP__