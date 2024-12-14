#include "serial.h"

#define SERIAL_COM3 0x3f8

#define INT_ENABLE_OFFSET 1

void serial_setup() {
  outb(SERIAL_COM3 + INT_ENABLE_OFFSET, 0x00);
  outb(SERIAL_COM3 + 3, 0x80);
  outb(SERIAL_COM3 + 0, 0x03);
  outb(SERIAL_COM3 + INT_ENABLE_OFFSET, 0x00);
  outb(SERIAL_COM3 + 3, 0x03);
  outb(SERIAL_COM3 + 2, 0xC7);
  outb(SERIAL_COM3 + 4, 0x0B);
}

int serial_is_transmit_empty(void) { return inb(SERIAL_COM3 + 5) & 0x20; }

// send data to SERIAL_COM3
void serial_putc(char c) {
  while (serial_is_transmit_empty() == 0)
    ;

  outb(SERIAL_COM3, c);
}

// send even more data to SERIAL_COM3
void serial_send_string(char *str) {
  for (int i = 0; str[i] != '\0'; i++)
    serial_putc(str[i]);
}

void serial_send_number(uint64_t num, int base) {
  char *str = "0123456789ABCDEF";
  static char buffer[128] = {0};
  int i = 0;
  if (num == 0) {
    buffer[i] = '0';
    i++;
  }
  while (num > 0) {
    buffer[i] = str[num % base];
    num /= base;
    i++;
  }
  buffer[i] = '\0';
  static char buffer2[128] = {0};
  buffer2[0] = '0';
  int j = 0;
  for (i = i - 1; i >= 0; i--) {
    buffer2[j] = buffer[i];
    j++;
  }
  buffer2[j] = '\0';
  serial_send_string(&buffer2[0]);
}

void serial_printf(const char *fmt, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  for (const char *p = fmt; *p != '\0'; p++) {
    if (*p != '%') {
      serial_putc(*p);
      continue;
    }
    p++;
    if (*p == 'd') {
      int i = __builtin_va_arg(args, int);
      serial_send_number(i, 10);
    } else if (*p == 'x') {
      uint64_t i = __builtin_va_arg(args, uint64_t);
      serial_send_number(i, 16);
    } else if (*p == 's') {
      char *s = __builtin_va_arg(args, char *);
      serial_send_string(s);
    } else if (*p == 'b') {
      uint64_t i = __builtin_va_arg(args, uint64_t);
      serial_send_number(i, 2);
    }
  }
  __builtin_va_end(args);
}