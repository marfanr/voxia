#include "init/init.h"
#include "input/keymap.h"
#include "ioforge/ioforge_usb.h"
#include "libk/serial.h"
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

KERNEL_API void input_report_key(struct ioforge_device* dev, uint16_t code,
                                 int value) {
	if (dev->type == IOFORGE_USB_DEVICE) {
		struct ioforge_usb_device* usb_dev =
		    (struct ioforge_usb_device*)dev;
		if (usb_dev->class_code != 0x3)
			return;
	}
	if (code == KEY_CAPS_LOCK && value)
		capslock ^= 1;

	static boolean_t shift = false;
	static uint16_t modifier = 0;

	if (code == LEFT_SHIFT || code == RIGHT_SHIFT) {
		shift = (value == 1);
		if (value == 1) {
			modifier |= (code == LEFT_SHIFT) ? MODIFIER_LSHIFT
			                                 : MODIFIER_RSHIFT;
		} else {
			modifier &= ~((code == LEFT_SHIFT) ? MODIFIER_LSHIFT
			                                   : MODIFIER_RSHIFT);
		}
		return;
	}

	if (code == LEFT_CTRL || code == RIGHT_CTRL) {
		if (value == 1) {
			if (code == LEFT_CTRL) {
				modifier |= MODIFIER_LCTRL;
				modifier &= ~MODIFIER_RCTRL;
			} else {
				modifier |= MODIFIER_RCTRL;
				modifier &= ~MODIFIER_LCTRL;
			}
		} else {
			modifier &= ~((code == LEFT_CTRL) ? MODIFIER_LCTRL
			                                  : MODIFIER_RCTRL);
		}
		return;
	}

	if (code == LEFT_ALT || code == RIGHT_ALT) {
		if (value == 1) {
			modifier |=
			    (code == LEFT_ALT) ? MODIFIER_LALT : MODIFIER_RALT;
		} else {
			modifier &= ~((code == LEFT_ALT) ? MODIFIER_LALT
			                                 : MODIFIER_RALT);
		}
		return;
	}

	if (code == LEFT_GUI || code == RIGHT_GUI) {
		if (value == 1) {
			modifier |=
			    (code == LEFT_GUI) ? MODIFIER_LGUI : MODIFIER_RGUI;
		} else {
			modifier &= ~((code == LEFT_GUI) ? MODIFIER_LGUI
			                                 : MODIFIER_RGUI);
		}
		return;
	}

	if (value != 1) {
		return;
	}

	const char* ascii = 0;
	if (code > 0)
		ascii = keycode_to_sequence(code, shift ^ capslock);

	// control sequence
	static char ctrl_seq[2] = {0};
	if ((modifier & MODIFIER_CTRL) && ascii && strlen(ascii) == 1) {
		char c = ascii[0];
		if (c >= 'a' && c <= 'z') {
			ctrl_seq[0] = (char)(c - 'a' + 1);
			ascii = ctrl_seq;
		} else if (c >= 'A' && c <= 'Z') {
			ctrl_seq[0] = (char)(c - 'A' + 1);
			ascii = ctrl_seq;
		} else if (c >= '0' && c <= '9') {
			ctrl_seq[0] = (char)(c - '0' + 1);
			ascii = ctrl_seq;
			serial2_printf("angka ctrl\n");
		} else if (c == '\\') {
			ctrl_seq[0] = 28;
			ascii = ctrl_seq;
		} else if (c == '[') {
			ctrl_seq[0] = 27;
			ascii = ctrl_seq;
		} else if (c == ']') {
			ctrl_seq[0] = 29;
			ascii = ctrl_seq;
		} else if (c == '^') {
			ctrl_seq[0] = 30;
			ascii = ctrl_seq;
		} else if (c == '_') {
			ctrl_seq[0] = 31;
			ascii = ctrl_seq;
		}
	}

	boolean_t send_as_key =
	    (modifier & (MODIFIER_ALT | MODIFIER_GUI)) ||
	    ((modifier & MODIFIER_CTRL) && (ascii != ctrl_seq)) || !ascii;

	if (send_as_key) {
		__atomic_store_n(&__event_data_ascii->type, INPUT_EVENT_KEY,
		                 __ATOMIC_RELAXED);
		__atomic_store_n(&__event_data_ascii->key.keycode, code,
		                 __ATOMIC_RELAXED);
		__atomic_store_n(&__event_data_ascii->key.pressed,
		                 (uint8_t)value, __ATOMIC_RELAXED);
		__atomic_store_n(&__event_data_ascii->key.modifiers, modifier,
		                 __ATOMIC_RELAXED);

		serial2_printf("[input_key]  mod ");
		for (int i = 0; i < 16; i++) {
			if (modifier & (1 << i))
				serial2_printf("%d ", i);
		}
		serial2_printf(" key %x (%s) value %d\n", code, ascii, value);
		notify_call("/input/triggered", 0, __event_data_ascii);
		return;
	}

	__atomic_store_n(&__event_data_ascii->text.codepoint, ascii,
	                 __ATOMIC_RELAXED);
	__atomic_store_n(&__event_data_ascii->type, INPUT_EVENT_TEXT,
	                 __ATOMIC_RELAXED);

	notify_call("/input/triggered", 0, __event_data_ascii);
}