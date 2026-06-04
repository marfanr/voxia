#include "init/init.h"
#include "input/keymap.h"
#include "ioforge/ioforge_usb.h"
#include "notify.h"
#include "type.h"
#include <input.h>
#include <ioforge/ioforge.h>
#include <str.h>

static struct input_event_data* __event_data_ascii = 0;
static boolean_t capslock = false;

INIT(Input) {
	notify_dev_create(str("/input/triggered"));
	__event_data_ascii =
	    (struct input_event_data*)kalloc(sizeof(struct input_event_data));
}

void input_report_key(struct ioforge_device* dev, uint16_t code, int value) {
	if (dev->type == IOFORGE_USB_DEVICE) {
		struct ioforge_usb_device* usb_dev =
		    (struct ioforge_usb_device*)dev;
		if (usb_dev->class_code != 0x3)
			return;
	}
	if (code == KEY_CAPS_LOCK && value)
		capslock ^= 1;

	static boolean_t shift = false;

	if (code == LEFT_SHIFT || code == RIGHT_SHIFT) {
		shift = (value == 1);
		return;
	}

	if (value != 1)
		return;

	const char* ascii = 0;
	if (code > 0)
		ascii = keycode_to_sequence(code, shift ^ capslock);

	if (ascii == 0) {
		__atomic_store_n(&__event_data_ascii->type,
						 INPUT_EVENT_KEY, __ATOMIC_RELAXED);
		__atomic_store_n(&__event_data_ascii->key.keycode, code,
						 __ATOMIC_RELAXED);
		__atomic_store_n(&__event_data_ascii->key.pressed,
						 (uint8_t)value, __ATOMIC_RELAXED);

		serial2_printf("input key:  mod 0 key %x value %d\n", code,
		               value);
		notify_call("/input/triggered", 0, __event_data_ascii);
		return;
	}

	__atomic_store_n(&__event_data_ascii->text.codepoint, ascii,
	                 __ATOMIC_RELAXED);
	__atomic_store_n(&__event_data_ascii->type, INPUT_EVENT_TEXT,
	                 __ATOMIC_RELAXED);

	notify_call("/input/triggered", 0, __event_data_ascii);
}