#include "input.h"
#include "ioforge/ioforge_int_pipe.hpp"
#include <str.h>
#include <usb-hid/keyboard.hpp>
#include <vfs/cache.h>
#include <vfs/dentry.h>

static HIDKeyboard* instance = 0;

HIDKeyboard::HIDKeyboard() { instance = this; }

void HIDKeyboard::init_vfs() {}

void HIDKeyboard::load(ioforge_usb_device* dev) {
	dev_ = dev;

	// setup usb interrupt pipe
	USBInterruptPipe* pipe = (USBInterruptPipe*)dev_->pipe;

	auto desc = (struct USBInterruptPipeDesc){
	    .dev_addr = dev_->addr,
	    .endpoint = (uint8_t)(dev_->endpoints[0].address & 0xF),
	    .speed = 2, // high speed
	    .interval_ms = dev_->endpoints[0].interval,
	    .buffer_size = 256,
	};
	pipe->open(desc, HIDKeyboard::fireHandler);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-designator"
// In a fully symbolic mode, we just map HID Usage ID to the enum value.
// We don't really need the [2] for shift here if we want the kernel to handle 
// the shift state, but to keep your "table" request, we'll map both to the same KEY_ enum.
static const uint16_t hid_keymap[256] = {
    [0x04] = KEY_A, [0x05] = KEY_B, [0x06] = KEY_C, [0x07] = KEY_D,
    [0x08] = KEY_E, [0x09] = KEY_F, [0x0A] = KEY_G, [0x0B] = KEY_H,
    [0x0C] = KEY_I, [0x0D] = KEY_J, [0x0E] = KEY_K, [0x0F] = KEY_L,
    [0x10] = KEY_M, [0x11] = KEY_N, [0x12] = KEY_O, [0x13] = KEY_P,
    [0x14] = KEY_Q, [0x15] = KEY_R, [0x16] = KEY_S, [0x17] = KEY_T,
    [0x18] = KEY_U, [0x19] = KEY_V, [0x1A] = KEY_W, [0x1B] = KEY_X,
    [0x1C] = KEY_Y, [0x1D] = KEY_Z,
    
    [0x1E] = KEY_1, [0x1F] = KEY_2, [0x20] = KEY_3, [0x21] = KEY_4,
    [0x22] = KEY_5, [0x23] = KEY_6, [0x24] = KEY_7, [0x25] = KEY_8,
    [0x26] = KEY_9, [0x27] = KEY_0,
    
    [0x28] = KEY_ENTER,
    [0x29] = KEY_ESC,
    [0x2A] = KEY_BACKSPACE,
    [0x2B] = KEY_TAB,
    [0x2C] = KEY_SPACE,
    
    [0x2D] = KEY_MINUS, [0x2E] = KEY_EQUAL, [0x2F] = KEY_LEFTBRACE,
    [0x30] = KEY_RIGHTBRACE, [0x31] = KEY_BACKSLASH, [0x32] = KEY_HASHTILDE,
    [0x33] = KEY_SEMICOLON, [0x34] = KEY_APOSTROPHE, [0x35] = KEY_GRAVE,
    [0x36] = KEY_COMMA, [0x37] = KEY_DOT, [0x38] = KEY_SLASH,
    [0x39] = KEY_CAPS_LOCK,

    // Function Keys
    [0x3A] = KEY_F1, [0x3B] = KEY_F2, [0x3C] = KEY_F3, [0x3D] = KEY_F4,
    [0x3E] = KEY_F5, [0x3F] = KEY_F6, [0x40] = KEY_F7, [0x41] = KEY_F8,
    [0x42] = KEY_F9, [0x43] = KEY_F10, [0x44] = KEY_F11, [0x45] = KEY_F12,

    // Navigation & Control
    [0x46] = KEY_PRINT_SCREEN, [0x47] = KEY_SCROLL_LOCK, [0x48] = KEY_PAUSE,
    [0x49] = KEY_INSERT, [0x4A] = KEY_HOME, [0x4B] = KEY_PAGE_UP,
    [0x4C] = KEY_DELETE, [0x4D] = KEY_END, [0x4E] = KEY_PAGE_DOWN,
    [0x4F] = KEY_RIGHT, [0x50] = KEY_LEFT, [0x51] = KEY_DOWN, [0x52] = KEY_UP,

    // Keypad
    [0x53] = KEY_NUM_LOCK, [0x54] = KEY_KP_DIVIDE, [0x55] = KEY_KP_MULTIPLY,
    [0x56] = KEY_KP_MINUS, [0x57] = KEY_KP_PLUS, [0x58] = KEY_KP_ENTER,
    [0x59] = KEY_KP_1, [0x5A] = KEY_KP_2, [0x5B] = KEY_KP_3, [0x5C] = KEY_KP_4,
    [0x5D] = KEY_KP_5, [0x5E] = KEY_KP_6, [0x5F] = KEY_KP_7, [0x60] = KEY_KP_8,
    [0x61] = KEY_KP_9, [0x62] = KEY_KP_0, [0x63] = KEY_KP_DOT,

    // International & Extra Keys (ThinkPad L14 specific / Regional)
    [0x87] = KEY_RO,               // Key between / and RShift
    [0x88] = KEY_KATAKANAHIRAGANA,
    [0x89] = KEY_YEN,              // Key near Backspace (International 3)
    [0x8A] = KEY_HENKAN,
    [0x8B] = KEY_MUHENKAN,
    [0x8C] = KEY_KPJPCOMMA,

    // Media Keys (Sometimes reported in Keyboard page)
    [0x7F] = KEY_MUTE,
    [0x80] = KEY_VOLUMEUP,
    [0x81] = KEY_VOLUMEDOWN,
    
    // Fn Key (Vendor specific or some laptops report 0xFF)
    [0xFF] = KEY_FN,
};
#pragma clang diagnostic pop

void HIDKeyboard::parse_report(const uint8_t* data, size_t len) {
	if (len < 8)
		return;

	uint8_t modifier = data[0];
	static uint8_t last_modifier = 0;

	// Handle Modifiers (Check for bit changes)
	uint16_t mod_keys[] = {
	    LEFT_CTRL,  LEFT_SHIFT,  LEFT_ALT,  LEFT_GUI,
	    RIGHT_CTRL, RIGHT_SHIFT, RIGHT_ALT, RIGHT_GUI,
	};

	for (int i = 0; i < 8; i++) {
		bool current = (modifier >> i) & 1;
		bool last = (last_modifier >> i) & 1;
		if (current != last) {
			input_report_key(&instance->dev_->controller->service,
			                 mod_keys[i], current ? 1 : 0);
		}
	}
	last_modifier = modifier;

	// Tracking previous keys to detect new presses (simplified)
	static uint8_t prev_keys[6] = {0};
	uint8_t current_keys[6];
	for (int i = 0; i < 6; i++)
		current_keys[i] = data[i + 2];

	for (int i = 0; i < 6; i++) {
		uint8_t key = current_keys[i];
		if (key == 0)
			continue;

		// Check if it's a new press
		bool is_new = true;
		for (int j = 0; j < 6; j++) {
			if (key == prev_keys[j]) {
				is_new = false;
				break;
			}
		}

		if (is_new) {
			uint16_t code = hid_keymap[key];
			if (code != KEY_NONE) {
				input_report_key(&instance->dev_->controller->service,
				                 code, 1);
		// 		// Immediately report release for simplicity in
		// 		// this stub
				input_report_key(&instance->dev_->controller->service,
				                 code, 0);
			}
		}
	}

	for (int i = 0; i < 6; i++)
		prev_keys[i] = current_keys[i];
}

void HIDKeyboard::fireHandler(const uint8_t* data, size_t len) {
	if (!instance) {
		return;
	}

	// parse report
	instance->parse_report(data, len);
}
