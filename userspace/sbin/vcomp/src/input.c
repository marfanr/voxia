
#include <errno.h>
#include <fcntl.h>
#include <input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vcomp.h>

/* Message types (mirror of vxterm's enum) */
enum vcomp_message_type {
	VCOMP_CREATE_WINDOW = 1,
	VCOMP_SUBMIT_RESOURCE = 2,
	VCOMP_DESTROY_WINDOW = 3,
	VCOMP_INPUT_REQUEST = 4,
	VCOMP_INPUT_RESPONSE = 5,
	VCOMP_SET_WINDOW = 6,
	VCOMP_LOG = 7,
	VCOMP_REPORT_KEYMAP = 8,
	VCOMP_KEY_EVENT = 9,
};

struct vcomp_msg_header {
	uint32_t type;
	uint32_t len;
};

/* Shift modifier state */
static int g_shift_held = 0;
static int g_ctrl_held = 0;

/* keyboard tracking */
char last_key_str[32] = "---";
static uint64_t last_key_time = 0;
int pos_x = 0;
int pos_y = 0;

bool right_button_just_pressed = false;
bool prev_right_button = false;

bool left_button_just_pressed = false;
bool prev_left_button = false;

const char* keycode_to_string(uint16_t code) {
	switch (code) {
	// ── Letters ──
	case KEY_A:
		return "A";
	case KEY_B:
		return "B";
	case KEY_C:
		return "C";
	case KEY_D:
		return "D";
	case KEY_E:
		return "E";
	case KEY_F:
		return "F";
	case KEY_G:
		return "G";
	case KEY_H:
		return "H";
	case KEY_I:
		return "I";
	case KEY_J:
		return "J";
	case KEY_K:
		return "K";
	case KEY_L:
		return "L";
	case KEY_M:
		return "M";
	case KEY_N:
		return "N";
	case KEY_O:
		return "O";
	case KEY_P:
		return "P";
	case KEY_Q:
		return "Q";
	case KEY_R:
		return "R";
	case KEY_S:
		return "S";
	case KEY_T:
		return "T";
	case KEY_U:
		return "U";
	case KEY_V:
		return "V";
	case KEY_W:
		return "W";
	case KEY_X:
		return "X";
	case KEY_Y:
		return "Y";
	case KEY_Z:
		return "Z";
	// ── Numbers ──
	case KEY_1:
		return "1";
	case KEY_2:
		return "2";
	case KEY_3:
		return "3";
	case KEY_4:
		return "4";
	case KEY_5:
		return "5";
	case KEY_6:
		return "6";
	case KEY_7:
		return "7";
	case KEY_8:
		return "8";
	case KEY_9:
		return "9";
	case KEY_0:
		return "0";
	// ── Control ──
	case KEY_ENTER:
		return "ENTER";
	case KEY_ESC:
		return "ESC";
	case KEY_BACKSPACE:
		return "BACKSPACE";
	case KEY_TAB:
		return "TAB";
	case KEY_SPACE:
		return "SPACE";
	// ── Symbols ──
	case KEY_MINUS:
		return "MINUS";
	case KEY_EQUAL:
		return "EQUAL";
	case KEY_LEFTBRACE:
		return "LEFTBRACE";
	case KEY_RIGHTBRACE:
		return "RIGHTBRACE";
	case KEY_BACKSLASH:
		return "BACKSLASH";
	case KEY_HASHTILDE:
		return "HASHTILDE";
	case KEY_SEMICOLON:
		return "SEMICOLON";
	case KEY_APOSTROPHE:
		return "APOSTROPHE";
	case KEY_GRAVE:
		return "GRAVE";
	case KEY_COMMA:
		return "COMMA";
	case KEY_DOT:
		return "DOT";
	case KEY_SLASH:
		return "SLASH";
	// ── Modifiers ──
	case LEFT_CTRL:
		return "LCTRL";
	case LEFT_SHIFT:
		return "LSHIFT";
	case LEFT_ALT:
		return "LALT";
	case LEFT_GUI:
		return "LGUI";
	case RIGHT_CTRL:
		return "RCTRL";
	case RIGHT_SHIFT:
		return "RSHIFT";
	case RIGHT_ALT:
		return "RALT";
	case RIGHT_GUI:
		return "RGUI";
	// ── Function ──
	case KEY_F1:
		return "F1";
	case KEY_F2:
		return "F2";
	case KEY_F3:
		return "F3";
	case KEY_F4:
		return "F4";
	case KEY_F5:
		return "F5";
	case KEY_F6:
		return "F6";
	case KEY_F7:
		return "F7";
	case KEY_F8:
		return "F8";
	case KEY_F9:
		return "F9";
	case KEY_F10:
		return "F10";
	case KEY_F11:
		return "F11";
	case KEY_F12:
		return "F12";
	// ── Navigation ──
	case KEY_PRINT_SCREEN:
		return "PRTSC";
	case KEY_SCROLL_LOCK:
		return "SCRLK";
	case KEY_PAUSE:
		return "PAUSE";
	case KEY_INSERT:
		return "INSERT";
	case KEY_HOME:
		return "HOME";
	case KEY_PAGE_UP:
		return "PGUP";
	case KEY_DELETE:
		return "DELETE";
	case KEY_END:
		return "END";
	case KEY_PAGE_DOWN:
		return "PGDN";
	case KEY_RIGHT:
		return "RIGHT";
	case KEY_LEFT:
		return "LEFT";
	case KEY_DOWN:
		return "DOWN";
	case KEY_UP:
		return "UP";
	// ── Keypad ──
	case KEY_NUM_LOCK:
		return "NUMLK";
	case KEY_KP_DIVIDE:
		return "KP/";
	case KEY_KP_MULTIPLY:
		return "KP*";
	case KEY_KP_MINUS:
		return "KP-";
	case KEY_KP_PLUS:
		return "KP+";
	case KEY_KP_ENTER:
		return "KPENTER";
	case KEY_KP_1:
		return "KP1";
	case KEY_KP_2:
		return "KP2";
	case KEY_KP_3:
		return "KP3";
	case KEY_KP_4:
		return "KP4";
	case KEY_KP_5:
		return "KP5";
	case KEY_KP_6:
		return "KP6";
	case KEY_KP_7:
		return "KP7";
	case KEY_KP_8:
		return "KP8";
	case KEY_KP_9:
		return "KP9";
	case KEY_KP_0:
		return "KP0";
	case KEY_KP_DOT:
		return "KP.";
	// ── Other ──
	case KEY_CAPS_LOCK:
		return "CAPS";
	default:
		return NULL;
	}
}

const char* mouse_button_to_string(uint16_t code) {
	switch (code) {
	case BTN_LEFT:
		return "BTN_LEFT";
	case BTN_RIGHT:
		return "BTN_RIGHT";
	case BTN_MIDDLE:
		return "BTN_MIDDLE";
	default:
		return NULL;
	}
}

uint16_t last_ev_type = 0;
uint16_t last_ev_code = 0;
int32_t last_ev_value = 0;
size_t last_ev_device_idx = 0;
uint32_t ev_seen_count = 0;

/* diagnostik: event/detik. Kalau ini kecil/nol pas mouse lagi digeser
 * terus-menerus, berarti event-nya emang gak nyampe dari kernel
 * (evdev.c belum di-rebuild dengan fix, atau drop lagi di driver USB) —
 * bukan bug di sisi compositor. */
uint32_t ev_rate_last_count = 0;
uint64_t ev_rate_window_start = 0;
uint32_t ev_rate_display = 0;

void update_event_rate(uint64_t now_ns) {
	if (ev_rate_window_start == 0) {
		ev_rate_window_start = now_ns;
		ev_rate_last_count = ev_seen_count;
		return;
	}
	uint64_t elapsed = now_ns - ev_rate_window_start;
	if (elapsed >= 1000000000ULL) { /* update tiap 1 detik */
		ev_rate_display = ev_seen_count - ev_rate_last_count;
		ev_rate_last_count = ev_seen_count;
		ev_rate_window_start = now_ns;
	}
}

/* rentang ABS mentah per device (diisi dari EVIOCGABS saat setup) */
static int32_t abs_min_x[2] = {0, 0};
static int32_t abs_max_x[2] = {32767, 32767};
static int32_t abs_min_y[2] = {0, 0};
static int32_t abs_max_y[2] = {32767, 32767};
static bool device_is_absolute[2] = {false, false};
static bool last_holded_ctrl = false;

void query_abs_range(int fd, size_t idx) {
	struct input_absinfo info;

	memset(&info, 0, sizeof(info));
	if (ioctl(fd, EVIOCGABS(ABS_X), &info) == 0 && info.maximum > info.minimum) {
		abs_min_x[idx] = info.minimum;
		abs_max_x[idx] = info.maximum;
		device_is_absolute[idx] = true;
	}

	memset(&info, 0, sizeof(info));
	if (ioctl(fd, EVIOCGABS(ABS_Y), &info) == 0 && info.maximum > info.minimum) {
		abs_min_y[idx] = info.minimum;
		abs_max_y[idx] = info.maximum;
		device_is_absolute[idx] = true;
	}
}

void read_ev_event(const struct input_event* ev, size_t device_idx) {
	if (!ev)
		return;

	last_ev_type = ev->type;
	last_ev_code = ev->code;
	last_ev_value = ev->value;
	last_ev_device_idx = device_idx;
	ev_seen_count++;
	if (ev_seen_count > UINT32_MAX) {
		ev_seen_count = 0;
	}

	/* Track modifier state */
	if (ev->type == EV_KEY) {
		if (ev->code == LEFT_SHIFT || ev->code == RIGHT_SHIFT) {
			g_shift_held = (ev->value != 0);
		}
		if (ev->code == LEFT_CTRL || ev->code == RIGHT_CTRL) {
			g_ctrl_held = (ev->value != 0);
		}
	}

	if (!ev->value && last_holded_ctrl) {
		last_holded_ctrl = false;
	}

	/* Track keyboard key */
	if (ev->type == EV_KEY && ev->code != BTN_LEFT && ev->code != BTN_RIGHT && ev->code != BTN_MIDDLE && ev->value == 1) {
		// for debugging
		const char* key = keycode_to_string(ev->code);
		if (key) {
			snprintf(last_key_str, sizeof(last_key_str), "%s", key);
		} else {
			snprintf(last_key_str, sizeof(last_key_str), "KEY_%u", ev->code);
		}

		/* ── Forward key to focused vxterm via IPC ── */
		if (active_client_fd >= 0) {
			const char* chars = NULL;
			char ctrl_char[2] = {0, 0};
			char plain_char[2] = {0, 0};

			/* Ctrl+key combinations */
			if (g_ctrl_held) {
				sock_log("g_ctrl_held on active client (%d)", active_client_fd);
				last_holded_ctrl = true;

				switch (ev->code) {
				case KEY_C:
					ctrl_char[0] = 0x03;
					chars = ctrl_char;
					sock_log("sigint detected");
					break; /* SIGINT */
				case KEY_D:
					ctrl_char[0] = 0x04;
					chars = ctrl_char;
					break; /* EOF */
				case KEY_L:
					ctrl_char[0] = 0x0C;
					chars = ctrl_char;
					break; /* clear */
				case KEY_Z:
					ctrl_char[0] = 0x1A;
					chars = ctrl_char;
					break; /* SIGTSTP */
				case KEY_A:
					ctrl_char[0] = 0x01;
					chars = ctrl_char;
					break;
				case KEY_E:
					ctrl_char[0] = 0x05;
					chars = ctrl_char;
					break;
				case KEY_U:
					ctrl_char[0] = 0x15;
					chars = ctrl_char;
					break;
				case KEY_K:
					ctrl_char[0] = 0x0B;
					chars = ctrl_char;
					break;
				case KEY_W:
					ctrl_char[0] = 0x17;
					chars = ctrl_char;
					break;
				default:
					break;
				}
			}

			/* Special keys */
			if (!chars) {
				switch (ev->code) {
				case KEY_ENTER:
					chars = "\n";
					break;
				case KEY_KP_ENTER:
					chars = "\n";
					break;
				case KEY_BACKSPACE:
					chars = "\x7f";
					break;
				case KEY_TAB:
					chars = "\t";
					break;
				case KEY_ESC:
					chars = "\x1b";
					break;
				/* Arrow keys: ANSI sequences */
				case KEY_UP:
					chars = "\x1b[A";
					break;
				case KEY_DOWN:
					chars = "\x1b[B";
					break;
				case KEY_RIGHT:
					chars = "\x1b[C";
					break;
				case KEY_LEFT:
					chars = "\x1b[D";
					break;
				case KEY_HOME:
					chars = "\x1b[1~";
					break;
				case KEY_END:
					chars = "\x1b[4~";
					break;
				case KEY_PAGE_UP:
					chars = "\x1b[5~";
					break;
				case KEY_PAGE_DOWN:
					chars = "\x1b[6~";
					break;
				case KEY_DELETE:
					chars = "\x1b[3~";
					break;
				/* Printable ASCII */
				case KEY_SPACE:
					chars = " ";
					break;
				case KEY_A:
					chars = g_shift_held ? "A" : "a";
					break;
				case KEY_B:
					chars = g_shift_held ? "B" : "b";
					break;
				case KEY_C:
					chars = g_shift_held ? "C" : "c";
					break;
				case KEY_D:
					chars = g_shift_held ? "D" : "d";
					break;
				case KEY_E:
					chars = g_shift_held ? "E" : "e";
					break;
				case KEY_F:
					chars = g_shift_held ? "F" : "f";
					break;
				case KEY_G:
					chars = g_shift_held ? "G" : "g";
					break;
				case KEY_H:
					chars = g_shift_held ? "H" : "h";
					break;
				case KEY_I:
					chars = g_shift_held ? "I" : "i";
					break;
				case KEY_J:
					chars = g_shift_held ? "J" : "j";
					break;
				case KEY_K:
					chars = g_shift_held ? "K" : "k";
					break;
				case KEY_L:
					chars = g_shift_held ? "L" : "l";
					break;
				case KEY_M:
					chars = g_shift_held ? "M" : "m";
					break;
				case KEY_N:
					chars = g_shift_held ? "N" : "n";
					break;
				case KEY_O:
					chars = g_shift_held ? "O" : "o";
					break;
				case KEY_P:
					chars = g_shift_held ? "P" : "p";
					break;
				case KEY_Q:
					chars = g_shift_held ? "Q" : "q";
					break;
				case KEY_R:
					chars = g_shift_held ? "R" : "r";
					break;
				case KEY_S:
					chars = g_shift_held ? "S" : "s";
					break;
				case KEY_T:
					chars = g_shift_held ? "T" : "t";
					break;
				case KEY_U:
					chars = g_shift_held ? "U" : "u";
					break;
				case KEY_V:
					chars = g_shift_held ? "V" : "v";
					break;
				case KEY_W:
					chars = g_shift_held ? "W" : "w";
					break;
				case KEY_X:
					chars = g_shift_held ? "X" : "x";
					break;
				case KEY_Y:
					chars = g_shift_held ? "Y" : "y";
					break;
				case KEY_Z:
					chars = g_shift_held ? "Z" : "z";
					break;
				case KEY_1:
					chars = g_shift_held ? "!" : "1";
					break;
				case KEY_2:
					chars = g_shift_held ? "@" : "2";
					break;
				case KEY_3:
					chars = g_shift_held ? "#" : "3";
					break;
				case KEY_4:
					chars = g_shift_held ? "$" : "4";
					break;
				case KEY_5:
					chars = g_shift_held ? "%" : "5";
					break;
				case KEY_6:
					chars = g_shift_held ? "^" : "6";
					break;
				case KEY_7:
					chars = g_shift_held ? "&" : "7";
					break;
				case KEY_8:
					chars = g_shift_held ? "*" : "8";
					break;
				case KEY_9:
					chars = g_shift_held ? "(" : "9";
					break;
				case KEY_0:
					chars = g_shift_held ? ")" : "0";
					break;
				case KEY_MINUS:
					chars = g_shift_held ? "_" : "-";
					break;
				case KEY_EQUAL:
					chars = g_shift_held ? "+" : "=";
					break;
				case KEY_LEFTBRACE:
					chars = g_shift_held ? "{" : "[";
					break;
				case KEY_RIGHTBRACE:
					chars = g_shift_held ? "}" : "]";
					break;
				case KEY_BACKSLASH:
					chars = g_shift_held ? "|" : "\\";
					break;
				case KEY_SEMICOLON:
					chars = g_shift_held ? ":" : ";";
					break;
				case KEY_APOSTROPHE:
					chars = g_shift_held ? "\"" : "'";
					break;
				case KEY_GRAVE:
					chars = g_shift_held ? "~" : "`";
					break;
				case KEY_COMMA:
					chars = g_shift_held ? "<" : ",";
					break;
				case KEY_DOT:
					chars = g_shift_held ? ">" : ".";
					break;
				case KEY_SLASH:
					chars = g_shift_held ? "?" : "/";
					break;
				/* Numpad */
				case KEY_KP_0:
					plain_char[0] = '0';
					chars = plain_char;
					break;
				case KEY_KP_1:
					plain_char[0] = '1';
					chars = plain_char;
					break;
				case KEY_KP_2:
					plain_char[0] = '2';
					chars = plain_char;
					break;
				case KEY_KP_3:
					plain_char[0] = '3';
					chars = plain_char;
					break;
				case KEY_KP_4:
					plain_char[0] = '4';
					chars = plain_char;
					break;
				case KEY_KP_5:
					plain_char[0] = '5';
					chars = plain_char;
					break;
				case KEY_KP_6:
					plain_char[0] = '6';
					chars = plain_char;
					break;
				case KEY_KP_7:
					plain_char[0] = '7';
					chars = plain_char;
					break;
				case KEY_KP_8:
					plain_char[0] = '8';
					chars = plain_char;
					break;
				case KEY_KP_9:
					plain_char[0] = '9';
					chars = plain_char;
					break;
				case KEY_KP_DOT:
					chars = ".";
					break;
				case KEY_KP_DIVIDE:
					chars = "/";
					break;
				case KEY_KP_MULTIPLY:
					chars = "*";
					break;
				case KEY_KP_MINUS:
					chars = "-";
					break;
				case KEY_KP_PLUS:
					chars = "+";
					break;
				default:
					break;
				}
			}

			/* Send key event message to vxterm via IPC */
			if (chars) {
				size_t clen = strlen(chars);
				size_t msg_size = sizeof(struct vcomp_msg_header) + clen;
				char* msgbuf = (char*)malloc(msg_size);
				if (msgbuf) {
					struct vcomp_msg_header* hdr = (struct vcomp_msg_header*)msgbuf;
					hdr->type = VCOMP_KEY_EVENT;
					hdr->len = (uint32_t)clen;
					memcpy(msgbuf + sizeof(*hdr), chars, clen);
					write(active_client_fd, msgbuf, msg_size);
					free(msgbuf);
				}
			}
		}

		last_key_time = time_ns();
	}

	if (ev->type == EV_REL) {
		if (ev->code == REL_X) {
			pos_x += ev->value;
		} else if (ev->code == REL_Y) {
			pos_y += ev->value;
		}

		if (pos_x < 0)
			pos_x = 0;
		if (pos_y < 0)
			pos_y = 0;
		if (pos_x > (int)g_screen_w - 1)
			pos_x = (int)g_screen_w - 1;
		if (pos_y > (int)g_screen_h - 1)
			pos_y = (int)g_screen_h - 1;

		need_rerender = 1;
	}

	/* device dengan pointer absolut (mis. usb-tablet virtual di QEMU)
	 * ngirim posisi langsung, bukan delta -> harus di-scale dari
	 * rentang mentahnya (biasa 0..32767) ke resolusi layar aktual. */
	if (ev->type == EV_ABS && device_is_absolute[device_idx]) {
		if (ev->code == ABS_X) {
			int32_t lo = abs_min_x[device_idx];
			int32_t hi = abs_max_x[device_idx];
			if (hi > lo) {
				pos_x = (int)((float)(ev->value - lo) / (float)(hi - lo) * (g_screen_w - 1.0f));
			}
		} else if (ev->code == ABS_Y) {
			int32_t lo = abs_min_y[device_idx];
			int32_t hi = abs_max_y[device_idx];
			if (hi > lo) {
				pos_y = (int)((float)(ev->value - lo) / (float)(hi - lo) * (g_screen_h - 1.0f));
			}
		}

		if (pos_x < 0)
			pos_x = 0;
		if (pos_y < 0)
			pos_y = 0;
		if (pos_x > (int)g_screen_w - 1)
			pos_x = (int)g_screen_w - 1;
		if (pos_y > (int)g_screen_h - 1)
			pos_y = (int)g_screen_h - 1;

		need_rerender = true;
	}

	if (ev->type == EV_KEY) {
		if (ev->code == BTN_RIGHT) {
			bool pressed = (ev->value != 0);

			if (pressed && !prev_right_button) {
				right_button_just_pressed = true;
			}
			prev_right_button = pressed;
		}

		if (ev->code == BTN_LEFT) {
			bool pressed = (ev->value != 0);

			if (pressed && !prev_left_button) {
				left_button_just_pressed = true;
				check_titlebar_click(pos_x, pos_y);
			} else if (!pressed && prev_left_button) {
				/* Button released - stop dragging */
				window_is_dragging = false;
			}
			prev_left_button = pressed;
		}
	}
}

void proces_event(int fd, size_t device_idx) {
	struct input_event ev;

	while (1) {
		ssize_t r = read(fd, &ev, sizeof(ev));
		if (r == sizeof(ev)) {
			read_ev_event(&ev, device_idx);
			continue;
		}
		if (r < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return;
			}
			return;
		}
		return;
	}
}

const char* evdev_detect_name(int fd) {
	char name[256];
	memset(name, 0, sizeof(name));
	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) == 0) {
		if (strstr(name, "mouse") || strstr(name, "Mouse") || strstr(name, "MOUSE")) {
			return "mouse";
		}
		if (strstr(name, "keyboard") || strstr(name, "Keyboard") || strstr(name, "KEYBOARD")) {
			return "keyboard";
		}
	}
	return NULL;
}

const char* evdev_detect_by_bits(int fd) {
	uint8_t bits[16];
	memset(bits, 0, sizeof(bits));
	if (ioctl(fd, EVIOCGBIT(0, sizeof(bits)), bits) == 0) {
		bool has_rel = bits[0] & (1 << EV_REL);
		bool has_key = bits[0] & (1 << EV_KEY);
		if (has_rel && has_key) {
			return "mouse";
		}
		if (has_key) {
			return "keyboard";
		}
	}
	return "unknown";
}

const char* evdev_detect_device(const char* path) {
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		return "unknown";
	}

	const char* result = evdev_detect_name(fd);
	if (!result) {
		result = evdev_detect_by_bits(fd);
	}
	close(fd);
	return result;
}

int open_evdev(const char* path) {
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		printf("Gagal membuka %s: %s\n", path, strerror(errno));
	}
	return fd;
}