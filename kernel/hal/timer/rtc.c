#include "libk/serial.h"
#include <hal/timer/rtc.h>
#include <libk/io.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t
cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static bool
rtc_is_updating()
{
    outb(CMOS_ADDR, 0x0A);
    return inb(CMOS_DATA) & 0x80;
}

typedef struct
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;

} date_time;

void
rtc_read_time(date_time *dt)
{
    while (rtc_is_updating())
        ; // tunggu UIP clear

    uint8_t sec   = cmos_read(0x00);
    uint8_t min   = cmos_read(0x02);
    uint8_t hour  = cmos_read(0x04);
    uint8_t day   = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year  = cmos_read(0x09);

    // BCD ke binary
    uint8_t regB = cmos_read(0x0B);
    if (!(regB & 0x04))
    {
        sec   = ((sec & 0x0F) + ((sec / 16) * 10));
        min   = ((min & 0x0F) + ((min / 16) * 10));
        hour  = ((hour & 0x0F) + (((hour / 16) * 10)));
        day   = ((day & 0x0F) + ((day / 16) * 10));
        month = ((month & 0x0F) + ((month / 16) * 10));
        year  = ((year & 0x0F) + ((year / 16) * 10));
    }

    // Format 12h ke 24h jika perlu
    if (!(regB & 0x02) && (hour & 0x80))
    {
        hour = ((hour & 0x7F) + 12) % 24;
    }
    dt->second = sec;
    dt->minute = min;
    dt->hour   = hour;
    dt->day    = day;
    dt->month  = month;
    dt->year   = year;
}

void
rtc_enable_periodic_interrupt()
{
    outb(CMOS_ADDR, 0x8A);
    uint8_t prevA = inb(CMOS_DATA);
    outb(CMOS_ADDR, 0x8A);
    outb(CMOS_DATA, (prevA & 0xF0) | 0x06); // 1024 Hz misalnya

    outb(CMOS_ADDR, 0x8B);
    uint8_t prevB = inb(CMOS_DATA);
    outb(CMOS_ADDR, 0x8B);
    outb(CMOS_DATA, prevB | 0x40); // Set PIE
}

void
rtc_isr()
{
    outb(CMOS_ADDR, 0x0C);
    inb(CMOS_DATA); // wajib dibaca untuk clear IRQ
    // ... lakukan hal lain seperti update jam global ...
}

void
rtc_initialize(void)
{
    date_time dt;
    rtc_read_time(&dt);
    LOG_INFO("RTC", "sekarang hari %d/%d/%d jam %d:%d:%d", dt.day, dt.month, dt.year, dt.hour,
             dt.minute, dt.second);
}
