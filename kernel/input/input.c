#include "init/init.h"
#include "input/keymap.h"
#include "ioforge/ioforge_usb.h"
#include "libk/serial.h"
#include "notify.h"
#include "type.h"
#include <graphic.h>
#include <input.h>
#include <ioforge/ioforge.h>
#include <str.h>

static struct input_event_data* __event_data_ascii = 0;
static boolean_t capslock = false;

extern void evdev_init();
extern void evdev_report_key(uint16_t code, int32_t value);
extern bool evdev_report_mouse(int16_t x, int16_t y, int8_t z, uint8_t buttons);

INIT(Input) {
	evdev_init();
	notify_dev_create(str("/input/triggered"));
	__event_data_ascii =
	    (struct input_event_data*)kalloc(sizeof(struct input_event_data));
}

KERNEL_API void input_report_key(struct ioforge_device* dev, uint16_t code,
                                 int value) {
	evdev_report_key(code, value);
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

KERNEL_API void input_report_mouse(struct ioforge_device* dev, int16_t x,
                                   int16_t y, int8_t z, uint8_t buttons) {
	/* Push ke evdev dulu. Kalau buffer penuh dan event di-drop,
	   JANGAN update hardware cursor — nanti desync dengan compositor
	   yang juga membaca dari evdev yang sama */
	bool evdev_ok = evdev_report_mouse(x, y, z, buttons);

	if (dev->type == IOFORGE_USB_DEVICE) {
		struct ioforge_usb_device* usb_dev =
		    (struct ioforge_usb_device*)dev;
		if (usb_dev->class_code != 0x3)
			return;
	}

	__atomic_store_n(&__event_data_ascii->type, INPUT_EVENT_MOUSE,
	                 __ATOMIC_RELAXED);
	__atomic_store_n(&__event_data_ascii->mouse.x, x, __ATOMIC_RELAXED);
	__atomic_store_n(&__event_data_ascii->mouse.y, y, __ATOMIC_RELAXED);
	__atomic_store_n(&__event_data_ascii->mouse.z, z, __ATOMIC_RELAXED);
	__atomic_store_n(&__event_data_ascii->mouse.buttons, buttons,
	                 __ATOMIC_RELAXED);

	/* Hanya update hardware cursor kalau evdev benar-benar
	   menerima event — jaga sinkronisasi antara posisi
	   cursor virtio-gpu dengan pos_x/pos_y compositor */
	if (!evdev_ok) {
		return;
	}

	static int cursor_x = -1;
	static int cursor_y = -1;

	struct graphic_device* gdev = graphic_get_device(0);
	if (cursor_x == -1 && cursor_y == -1) {
		/* Inisialisasi posisi cursor ke center. Kalau GPU belum
		   siap, pakai default 1920x1080 — sama dengan hardcode
		   di compositor. Jangan fallback ke (0,0) karena compositor
		   selalu mulai dari center, nanti offset permanen. */
		int cx = 960, cy = 540;
		if (gdev && gdev->scanout_list) {
			cx = gdev->scanout_list->width / 2;
			cy = gdev->scanout_list->height / 2;
		}
		cursor_x = cx;
		cursor_y = cy;
	}

	cursor_x += x;
	cursor_y += y;

	if (cursor_x < 0)
		cursor_x = 0;
	if (cursor_y < 0)
		cursor_y = 0;

	// struct graphic_device* gdev = graphic_get_device(0);
	if (gdev && gdev->scanout_list && gdev->ops && gdev->ops->move_cursor) {
		// serial2_printf("[input_mouse] x %d y %d z %d buttons %x\n",
		//                cursor_x, cursor_y, z, buttons);
					   
		if (cursor_x > gdev->scanout_list->width)
			cursor_x = gdev->scanout_list->width;
		if (cursor_y > gdev->scanout_list->height)
			cursor_y = gdev->scanout_list->height;

		gdev->ops->move_cursor(gdev, gdev->scanout_list,
		                       (uint32_t)cursor_x, (uint32_t)cursor_y);
	}

	// notify_call("/input/triggered", 0, __event_data_ascii);
}