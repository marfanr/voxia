#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct input_event {
	uint64_t tv_sec;
	uint64_t tv_usec;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

#define SYN_REPORT 0

#define REL_X 0x00
#define REL_Y 0x01
#define REL_Z 0x02

#define ABS_X 0x00
#define ABS_Y 0x01

#define BTN_MOUSE 0x110
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

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

#ifndef EVIOCGNAME
#define EVIOCGNAME(len) _IOC(_IOC_READ, 'E', 0x06, len)
#endif
#ifndef EVIOCGBIT
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), len)
#endif

struct input_absinfo {
	int32_t value;
	int32_t minimum;
	int32_t maximum;
	int32_t fuzz;
	int32_t flat;
	int32_t resolution;
};

#ifndef EVIOCGABS
#define EVIOCGABS(abs) _IOR('E', 0x40 + (abs), struct input_absinfo)
#endif

const char* keycode_to_string(uint16_t code);
const char* mouse_button_to_string(uint16_t code);

void proces_event(int fd, size_t device_idx);
void read_ev_event(const struct input_event* ev, size_t device_idx);
void update_event_rate(uint64_t now_ns);
void query_abs_range(int fd, size_t idx);
const char* evdev_detect_name(int fd);
const char* evdev_detect_by_bits(int fd);
const char* evdev_detect_device(const char* path);

int open_evdev(const char* path);

// for now

extern uint16_t last_ev_type;
extern uint16_t last_ev_code;
extern int32_t last_ev_value;
extern size_t last_ev_device_idx;
extern uint32_t ev_seen_count;
extern uint32_t ev_rate_last_count;
extern uint64_t ev_rate_window_start;
extern uint32_t ev_rate_display;
extern int pos_x;
extern int pos_y;

extern bool right_button_just_pressed;
extern bool prev_right_button;
extern bool left_button_just_pressed;
extern bool prev_left_button;

extern bool window_is_dragging;
extern char last_key_str[32];

#endif