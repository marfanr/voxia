#include "type.h"
#include <input.h>
#include <input/keymap.h>

static const char keymap_us[512][2] = {
    [KEY_A] = {'a', 'A'},
    [KEY_B] = {'b', 'B'},
    [KEY_C] = {'c', 'C'},
    [KEY_D] = {'d', 'D'},
    [KEY_E] = {'e', 'E'},
    [KEY_F] = {'f', 'F'},
    [KEY_G] = {'g', 'G'},
    [KEY_H] = {'h', 'H'},
    [KEY_I] = {'i', 'I'},
    [KEY_J] = {'j', 'J'},
    [KEY_K] = {'k', 'K'},
    [KEY_L] = {'l', 'L'},
    [KEY_M] = {'m', 'M'},
    [KEY_N] = {'n', 'N'},
    [KEY_O] = {'o', 'O'},
    [KEY_P] = {'p', 'P'},
    [KEY_Q] = {'q', 'Q'},
    [KEY_R] = {'r', 'R'},
    [KEY_S] = {'s', 'S'},
    [KEY_T] = {'t', 'T'},
    [KEY_U] = {'u', 'U'},
    [KEY_V] = {'v', 'V'},
    [KEY_W] = {'w', 'W'},
    [KEY_X] = {'x', 'X'},
    [KEY_Y] = {'y', 'Y'},
    [KEY_Z] = {'z', 'Z'},

    [KEY_1] = {'1', '!'},
    [KEY_2] = {'2', '@'},
    [KEY_3] = {'3', '#'},
    [KEY_4] = {'4', '$'},
    [KEY_5] = {'5', '%'},
    [KEY_6] = {'6', '^'},
    [KEY_7] = {'7', '&'},
    [KEY_8] = {'8', '*'},
    [KEY_9] = {'9', '('},
    [KEY_0] = {'0', ')'},

    [KEY_ENTER] = {'\n', '\n'},
    [KEY_BACKSPACE] = {'\b', '\b'},
    [KEY_TAB] = {'\t', '\t'},
    [KEY_SPACE] = {' ', ' '},

    [KEY_MINUS] = {'-', '_'},
    [KEY_EQUAL] = {'=', '+'},
    [KEY_LEFTBRACE] = {'[', '{'},
    [KEY_RIGHTBRACE] = {']', '}'},
    [KEY_BACKSLASH] = {'\\', '|'},
    [KEY_SEMICOLON] = {';', ':'},
    [KEY_APOSTROPHE] = {'\'', '\"'},
    [KEY_GRAVE] = {'`', '~'},
    [KEY_COMMA] = {',', '<'},
    [KEY_DOT] = {'.', '>'},
    [KEY_SLASH] = {'/', '?'},
};

uint16_t scancode_to_ascii(uint16_t scancode, boolean_t shift) {
    int shift_idx = shift ? 1 : 0;
    
	if ((size_t)scancode < sizeof(keymap_us)) {
		return (uint16_t)keymap_us[scancode][shift_idx];
	}
	return 0;
}