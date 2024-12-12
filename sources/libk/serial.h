#ifndef __LIBK_SERIAL_H__
#define __LIBK_SERIAL_H__

#include "io.h"

void serial_setup();
int serial_is_transmit_empty(void);
void serial_send_string(char *str);
void serial_send_number(uint64_t num, int base);
void serial_printf(const char *fmt, ...);

#define serial_trace(...) serial_printf(__VA_ARGS__)

#endif /* __LIBK_SERIAL_H__ */