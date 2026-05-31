#ifndef __INPUT_H__
#define __INPUT_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

enum INPUT_KEY : uint16_t {
	KEY_NONE = 0,

	// Letters
	KEY_A = 0x04,
	KEY_B,
	KEY_C,
	KEY_D,
	KEY_E,
	KEY_F,
	KEY_G,
	KEY_H,
	KEY_I,
	KEY_J,
	KEY_K,
	KEY_L,
	KEY_M,
	KEY_N,
	KEY_O,
	KEY_P,
	KEY_Q,
	KEY_R,
	KEY_S,
	KEY_T,
	KEY_U,
	KEY_V,
	KEY_W,
	KEY_X,
	KEY_Y,
	KEY_Z,

	// Numbers
	KEY_1 = 0x1E,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_0,

	// Control & Whitespace
	KEY_ENTER = 0x28,
	KEY_ESC = 0x29,
	KEY_BACKSPACE = 0x2A,
	KEY_TAB = 0x2B,
	KEY_SPACE = 0x2C,

	// Symbols
	KEY_MINUS = 0x2D,      // - and _
	KEY_EQUAL = 0x2E,      // = and +
	KEY_LEFTBRACE = 0x2F,  // [ and {
	KEY_RIGHTBRACE = 0x30, // ] and }
	KEY_BACKSLASH = 0x31,  // \ and |
	KEY_HASHTILDE = 0x32,  // # and ~
	KEY_SEMICOLON = 0x33,  // ; and :
	KEY_APOSTROPHE = 0x34, // ' and "
	KEY_GRAVE = 0x35,      // ` and ~
	KEY_COMMA = 0x36,      // , and <
	KEY_DOT = 0x37,        // . and >
	KEY_SLASH = 0x38,      // / and ?

	// Modifiers (Requested at 0xA0)
	LEFT_CTRL = 0xA0,
	LEFT_SHIFT,
	LEFT_ALT,
	LEFT_GUI,
	RIGHT_CTRL,
	RIGHT_SHIFT,
	RIGHT_ALT,
	RIGHT_GUI,

	// Function Keys
	KEY_F1 = 0xF1,
	KEY_F2,
	KEY_F3,
	KEY_F4,
	KEY_F5,
	KEY_F6,
	KEY_F7,
	KEY_F8,
	KEY_F9,
	KEY_F10,
	KEY_F11,
	KEY_F12,

	// Navigation & Control
	KEY_PRINT_SCREEN = 0xA10,
	KEY_SCROLL_LOCK,
	KEY_PAUSE,
	KEY_INSERT,
	KEY_HOME,
	KEY_PAGE_UP,
	KEY_DELETE,
	KEY_END,
	KEY_PAGE_DOWN,
	KEY_RIGHT,
	KEY_LEFT,
	KEY_DOWN,
	KEY_UP,

	// Keypad
	KEY_NUM_LOCK,
	KEY_KP_DIVIDE,
	KEY_KP_MULTIPLY,
	KEY_KP_MINUS,
	KEY_KP_PLUS,
	KEY_KP_ENTER,
	KEY_KP_1,
	KEY_KP_2,
	KEY_KP_3,
	KEY_KP_4,
	KEY_KP_5,
	KEY_KP_6,
	KEY_KP_7,
	KEY_KP_8,
	KEY_KP_9,
	KEY_KP_0,
	KEY_KP_DOT,

	// Other
	KEY_CAPS_LOCK,
	KEY_FN,

	// International & Special (e.g., ThinkPad, Japanese, Brazilian layouts)
	KEY_RO = 0xC0, // International 1 (Brazilian / or ThinkPad between / and
	               // RShift)
	KEY_KATAKANAHIRAGANA,
	KEY_YEN, // International 3 (Japanese Yen or ThinkPad near Backspace)
	KEY_HENKAN,
	KEY_MUHENKAN,
	KEY_KPJPCOMMA,
	KEY_INTL4,
	KEY_INTL5,
	KEY_INTL6,

	// Media & Extra (Standard USB HID Consumer Page mappings often handled
	// via separate report, but some are in Keyboard Page)
	KEY_MUTE,
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
};

struct ioforge_device;
void input_report_key(struct ioforge_device* dev, uint16_t code, int value);


enum {
    INPUT_EVENT_KEY,
    INPUT_EVENT_TEXT,
};

struct input_event_data {
    uint16_t type;

    union {
        struct {
            uint16_t keycode;
            uint16_t modifiers;
            uint8_t pressed;
        } key;

        struct {
            const char* codepoint;
        } text;
    };
};

#ifdef __cplusplus
}
#endif

#endif // __INPUT_H__