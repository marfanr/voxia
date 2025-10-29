#ifndef __LIBK_SERIAL_H__
#define __LIBK_SERIAL_H__

#define SERIAL_COM1 0x3f8
#define SERIAL_COM2 0x2f8
#define SERIAL_COM3 0x3e8
#define SERIAL_COM4 0x2e8

#include <libk/type.h>

int  serial_is_transmit_empty(void);
void serial_send_string(char *str);
void serial_send_number(int64_t num, int base);
void serial_printf(const char *fmt, ...);
void serial_putc(char c);
void serial_clear();
void serial_setup();

#define serial_trace(...) serial_printf(__VA_ARGS__)
#define LOG_INFO(mod, fmt, ...) serial_printf("[INFO][%s] " fmt "\n", mod, ##__VA_ARGS__)
#define LOG_DEBUG(mod, fmt, ...)                                                                   \
    serial_printf("[DEBUG][%s][%s:%d] " fmt "\n", mod, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(mod, fmt, ...) serial_printf("[ERROR][%s] " fmt "\n", mod, ##__VA_ARGS__)
#define LOG_WARN(mod, fmt, ...) serial_printf("[WARN][%s] " fmt "\n", mod, ##__VA_ARGS__)

#endif /* __LIBK_SERIAL_H__ */