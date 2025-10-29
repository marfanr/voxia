#include "serial.h"
#include "libk/type.h"
#include <init/init.h>
#include <libk/io.h>

#define INT_ENABLE_OFFSET 1

void
serial_setup()
{
    outb(SERIAL_COM1 + INT_ENABLE_OFFSET, 0x00);
    outb(SERIAL_COM1 + 3, 0x80);
    outb(SERIAL_COM1 + 0, 0x03);
    outb(SERIAL_COM1 + INT_ENABLE_OFFSET, 0x00);
    outb(SERIAL_COM1 + 3, 0x03);
    outb(SERIAL_COM1 + 2, 0xC7);
    outb(SERIAL_COM1 + 4, 0x0B);
}

int
serial_is_transmit_empty(void)
{
    return inb(SERIAL_COM1 + 5) & 0x20;
}

// send data to SERIAL_COM1
void
serial_putc(char c)
{
    while (serial_is_transmit_empty() == 0)
        ;

    outb(SERIAL_COM1, c);
}

// send even more data to SERIAL_COM1
void
serial_send_string(char *str)
{
    for (int i = 0; str[i] != '\0'; i++)
        serial_putc(str[i]);
}

void
serial_send_number(int64_t num, int base)
{
    const char *digits = "0123456789ABCDEF";
    char        buffer[32]; // cukup untuk 64-bit
    int         i        = 0;
    bool        negative = false;

    if (base == 10 && num < 0)
    {
        negative = true;
        num      = -num; // ubah ke positif untuk konversi desimal
    }

    // gunakan uint64_t untuk base != 10 agar bisa cetak semua bit
    uint64_t n = (base == 10) ? (uint64_t)num : (uint64_t)num;

    if (n == 0)
    {
        buffer[i++] = '0';
    }
    else
    {
        while (n > 0)
        {
            buffer[i++] = digits[n % base];
            n /= base;
        }
    }

    if (negative)
    {
        buffer[i++] = '-';
    }

    // kirim dalam urutan normal
    for (int j = i - 1; j >= 0; j--)
    {
        serial_putc(buffer[j]);
    }
}

void
serial_send_number_double(double value, int precision)
{
    // Tangani nilai negatif
    if (value < 0)
    {
        serial_putc('-');
        value = -value;
    }

    // Ambil bagian integer
    uint64_t integer_part = (uint64_t)value;

    // Ambil bagian pecahan
    double fraction = value - (double)integer_part;

    // Kirim bagian integer (pakai fungsi kamu sebelumnya)
    {
        const char *digits = "0123456789";
        char        buffer[32];
        int         i = 0;

        if (integer_part == 0)
        {
            buffer[i++] = '0';
        }
        else
        {
            while (integer_part > 0)
            {
                buffer[i++] = digits[integer_part % 10];
                integer_part /= 10;
            }
        }

        for (int j = i - 1; j >= 0; j--)
        {
            serial_putc(buffer[j]);
        }
    }

    serial_putc('.');

    // Cetak bagian pecahan
    for (int i = 0; i < precision; i++)
    {
        fraction *= 10.0;
        int digit = (int)fraction;
        serial_putc('0' + digit);
        fraction -= digit;
    }
}

KERNEL_API
void
serial_printf(const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    // bool use_limitation = false;

    for (const char *p = fmt; *p != '\0'; p++)
    {
        if (*p != '%')
        {
            serial_putc(*p);
            continue;
        }
        p++;
        // if (*p == '.')
        // {
        //     use_limitation = true;
        //     p++;
        //     // check is numeric
        //     if (*p >= '0' && *p <= '9')
        //     {
        // }
        if (*p == 'd')
        {
            int i = __builtin_va_arg(args, int);
            serial_send_number(i, 10);
        }
        else if (*p == 'l')
        {
            p++;
            if (*p == 'u')
            {
                uint64_t i = __builtin_va_arg(args, uint64_t);
                serial_send_number(i, 10);
            }
        }
        else if (*p == 'x')
        {
            uint64_t i = __builtin_va_arg(args, uint64_t);
            serial_send_number(i, 16);
        }
        else if (*p == 's')
        {
            char *s = __builtin_va_arg(args, char *);
            serial_send_string(s);
        }
        else if (*p == 'b')
        {
            uint64_t i = __builtin_va_arg(args, uint64_t);
            serial_send_number(i, 2);
        }
        else if (*p == 'f')
        {
            double f = __builtin_va_arg(args, double);
            serial_send_number_double(f, 2);
        }
        else if (*p == '.')
        {
            p++;
            int precision = 0;
            while (*p >= '0' && *p <= '9')
            {
                precision = precision * 10 + (*p - '0');
                p++;
            }
            if (*p == 'f')
            {
                double f = __builtin_va_arg(args, double);
                serial_send_number_double(f, precision);
            }
        }
    }
    __builtin_va_end(args);
}

void
serial_clear()
{
    serial_printf("\033[2J\033[H");
}