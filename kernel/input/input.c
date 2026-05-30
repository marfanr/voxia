#include "init/init.h"
#include "input/keymap.h"
#include "ioforge/ioforge_usb.h"
#include "notify.h"
#include "type.h"
#include <input.h>
#include <ioforge/ioforge.h>

static struct input_event_data* __event_data = 0;
INIT(Input) {
	notify_dev_create(str("/input/trigered"));
	__event_data =
	    (struct input_event_data*)kalloc(sizeof(struct input_event_data));
}

void input_report_key(struct ioforge_device* dev, uint16_t code, int value) {
    if (dev->type == IOFORGE_USB_DEVICE) {
        struct ioforge_usb_device* usb_dev = (struct ioforge_usb_device*)dev;
        if (usb_dev->class_code != 0x3)
            return;
    }

    static boolean_t shift = false;

    if (code == LEFT_SHIFT || code == RIGHT_SHIFT || code == KEY_CAPS_LOCK) {
        shift = (value == 1);
        return;
    }

    if (value != 1)
        return;

    uint16_t ascii = 0;
    if (code < LEFT_CTRL && code > 0)
        ascii = scancode_to_ascii(code, shift);

    if (ascii == 0)
        return;

    // serial2_printf("input: code=%x ascii=%x\n", code, ascii);

    __atomic_store_n(&__event_data->code, ascii, __ATOMIC_RELAXED);
    __atomic_store_n(&__event_data->input_active, 1, __ATOMIC_RELAXED);

    notify_call("/input/trigered", 0, __event_data);
}