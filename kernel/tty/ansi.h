#ifndef __TTY_ANSI_H__
#define __TTY_ANSI_H__

#include <type.h>

struct tty_internal;

int ansi_process_char(struct tty_internal* priv, uint8_t c);

void ansi_handle_color(struct tty_internal* priv);
void ansi_handle_cursor(struct tty_internal* priv, uint8_t c, int p0, int p1);
void ansi_handle_erase(struct tty_internal* priv, uint8_t c, int p0);

#endif // __TTY_ANSI_H__
