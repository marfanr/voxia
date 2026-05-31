#ifndef __INPUT_KEYMAP_H__
#define __INPUT_KEYMAP_H__

#include <type.h>

const char* keycode_to_sequence(uint16_t scancode, boolean_t shift);

#endif // __INPUT_KEYMAP_H__