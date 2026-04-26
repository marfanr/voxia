#include "serial.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/spinlock.h"
#include "libk/type.h"
#include <init/init.h>
#include <libk/io.h>
#include <libk/str.h>

#define INT_ENABLE_OFFSET 1

typedef struct {
	char data[128];
	uint8_t len;
} serial_entry_t;

typedef struct {
	serial_entry_t buffer[8192];
	uint32_t head;
	uint32_t tail;
} serial_ring_buffer_t;

static serial_ring_buffer_t __buffer = {0};

void serial_setup() {
	outb(SERIAL_COM1 + INT_ENABLE_OFFSET, 0x00);
	outb(SERIAL_COM1 + 3, 0x80);
	outb(SERIAL_COM1 + 0, 0x03);
	outb(SERIAL_COM1 + INT_ENABLE_OFFSET, 0x00);
	outb(SERIAL_COM1 + 3, 0x03);
	outb(SERIAL_COM1 + 2, 0xC7);
	outb(SERIAL_COM1 + 4, 0x0B);
}

int serial_is_transmit_empty(void) { return inb(SERIAL_COM1 + 5) & 0x20; }

// send data to SERIAL_COM1
extern void serial_putc(char c) {
	while (serial_is_transmit_empty() == 0)
		;

	outb(SERIAL_COM1, c);
}

// send even more data to SERIAL_COM1
void serial_send_string(char* str) {
	for (int i = 0; str[i] != '\0'; i++)
		serial_putc(str[i]);
}

void serial_send_number(int64_t num, int base) {
	const char* digits = "0123456789ABCDEF";
	char buffer[32]; // cukup untuk 64-bit
	int i = 0;
	bool negative = false;

	if (base == 10 && num < 0) {
		negative = true;
		num = -num; // ubah ke positif untuk konversi desimal
	}

	// gunakan uint64_t untuk base != 10 agar bisa cetak semua bit
	uint64_t n = (base == 10) ? (uint64_t)num : (uint64_t)num;

	if (n == 0) {
		buffer[i++] = '0';
	} else {
		while (n > 0) {
			buffer[i++] = digits[n % base];
			n /= base;
		}
	}

	if (negative) {
		buffer[i++] = '-';
	}

	// kirim dalam urutan normal
	for (int j = i - 1; j >= 0; j--) {
		serial_putc(buffer[j]);
	}
}

void serial_send_unsigned_number(uint64_t num, int base, int limit) {
	const char* digits = "0123456789ABCDEF";
	char buffer[32];
	int i = 0;

	if (base < 2 || base > 16) {
		serial_putc('?');
		return;
	}

	uint64_t n = num;

	// generate digits reversed
	if (n == 0) {
		buffer[i++] = '0';
	} else {
		while (n > 0 && i < 32) {
			buffer[i++] = digits[n % base];
			n /= base;
		}
	}

	// Tentukan jumlah digit yang ingin dicetak
	int count = (limit > 0 && limit < i) ? limit : i;

	// Hitung posisi mulai: ambil dari MSB
	int start = i - count; // digit paling signifikan
	int end = i - 1;       // index digit paling besar

	// Print dari MSB ke LSB
	for (int j = end; j >= start; j--) {
		serial_putc(buffer[j]);
	}
}

void serial_send_number_double(double value, int precision) {
	// Tangani nilai negatif
	if (value < 0) {
		serial_putc('-');
		value = -value;
	}

	// Ambil bagian integer
	uint64_t integer_part = (uint64_t)value;

	// Ambil bagian pecahan
	double fraction = value - (double)integer_part;

	// Kirim bagian integer (pakai fungsi kamu sebelumnya)
	{
		const char* digits = "0123456789";
		char buffer[32];
		int i = 0;

		if (integer_part == 0) {
			buffer[i++] = '0';
		} else {
			while (integer_part > 0) {
				buffer[i++] = digits[integer_part % 10];
				integer_part /= 10;
			}
		}

		for (int j = i - 1; j >= 0; j--) {
			serial_putc(buffer[j]);
		}
	}

	serial_putc('.');

	// Cetak bagian pecahan
	for (int i = 0; i < precision; i++) {
		fraction *= 10.0;
		int digit = (int)fraction;
		serial_putc('0' + digit);
		fraction -= digit;
	}
}

void serial_clear() { serial_printf("\033[2J\033[H"); }

// untuk debug yang tidak boleh mengakibatkan deadlock, misal di interrupt
// handler atau di dalam spinlock
// serial nanti akan di migrasi ke sini
static void put_into_buffer(const char* str, uint8_t len) {
	uint32_t idx = __atomic_fetch_add(&__buffer.head, 1, __ATOMIC_RELAXED);
	idx %= 8192;
	serial_entry_t* entry = &__buffer.buffer[idx];
	for (uint8_t i = 0; i < len; i++) {
		entry->data[i] = str[i];
	}
	__atomic_store_n(&entry->len, len, __ATOMIC_RELEASE);
}

static void serial2_send_number(int64_t num, int base) {
	const char* digits = "0123456789ABCDEF";
	char buffer[32];
	int i = 0;
	bool negative = false;

	if (base == 10 && num < 0) {
		negative = true;
		num = -num;
	}

	uint64_t n = (uint64_t)num;

	if (n == 0) {
		buffer[i++] = '0';
	} else {
		while (n > 0) {
			buffer[i++] = digits[n % base];
			n /= base;
		}
	}

	if (negative) {
		buffer[i++] = '-';
	}

	char buffer2[32] = {0};
	for (int j = i - 1; j >= 0; j--) {
		buffer2[i - j - 1] = buffer[j];
	}
	put_into_buffer(buffer2, i);
}

static void serial2_send_unsigned_number(uint64_t num, int base, int limit) {
	const char* digits = "0123456789ABCDEF";
	char buffer[32];
	int i = 0;

	if (base < 2 || base > 16) {
		serial_putc('?');
		return;
	}

	uint64_t n = num;

	if (n == 0) {
		buffer[i++] = '0';
	} else {
		while (n > 0 && i < 32) {
			buffer[i++] = digits[n % base];
			n /= base;
		}
	}

	int count = (limit > 0 && limit < i) ? limit : i;
	int start = i - count;
	int end = i - 1;

	char buffer2[32] = {0};
	for (int j = end; j >= start; j--) {
		buffer2[end - j] = buffer[j];
	}
	// BUG 4 FIX: gunakan 'count', bukan 'i'
	put_into_buffer(buffer2, count);
}

static void serial2_send_number_double(double value, int precision) {
	// BUG 2 FIX: gunakan put_into_buffer, bukan serial_putc langsung
	if (value < 0) {
		put_into_buffer("-", 1);
		value = -value;
	}

	uint64_t integer_part = (uint64_t)value;
	double fraction = value - (double)integer_part;

	{
		const char* digits = "0123456789";
		char buffer[32];
		int i = 0;

		if (integer_part == 0) {
			buffer[i++] = '0';
		} else {
			while (integer_part > 0) {
				buffer[i++] = digits[integer_part % 10];
				integer_part /= 10;
			}
		}

		char buffer2[32] = {0};
		for (int j = i - 1; j >= 0; j--) {
			buffer2[i - j - 1] = buffer[j];
		}
		put_into_buffer(buffer2, i);
	}

	put_into_buffer(".", 1);

	for (int i = 0; i < precision; i++) {
		fraction *= 10.0;
		int digit = (int)fraction;
		char outb[2] = {0};
		outb[0] = '0' + digit;
		put_into_buffer(outb, 1);
		fraction -= digit;
	}
}

static void parse_multicore(__builtin_va_list args, const char* fmt) {
	char temp_buffer[128];
	uint16_t temp_index = 0;

	for (const char* p = fmt; *p != '\0'; p++) {
		if (*p != '%') {
			temp_buffer[temp_index++] = *p;
			if (temp_index == 128) {
				put_into_buffer(temp_buffer, temp_index);
				temp_index = 0;
			}
			continue;
		}
		if (temp_index > 0) {
			put_into_buffer(temp_buffer, temp_index);
			temp_index = 0;
		}

		p++;
		if (*p == 'd') {
			int i = __builtin_va_arg(args, int);
			serial2_send_number(i, 10);
		} else if (*p == 'l') {
			p++;
			if (*p == 'u') {
				uint64_t i = __builtin_va_arg(args, uint64_t);
				serial2_send_unsigned_number(i, 10, 0);
			} else if (*p == 'd') {
				int64_t i = __builtin_va_arg(args, int64_t);
				serial2_send_number(i, 10);
			}
		} else if (*p == 'x') {
			uint64_t i = __builtin_va_arg(args, uint64_t);
			serial2_send_number(i, 16);
		} else if (*p == 's') {
			char* s = __builtin_va_arg(args, char*);
			put_into_buffer(s, strlen(s));
		} else if (*p == 'b') {
			uint64_t i = __builtin_va_arg(args, uint64_t);
			serial2_send_number(i, 2);
		} else if (*p == 'B') {
			boolean_t i = __builtin_va_arg(args, boolean_t);
			if (i) {
				// BUG 3 FIX: "True" = 4 karakter, bukan 5
				put_into_buffer("True", 4);
			} else {
				put_into_buffer("False", 5);
			}
		} else if (*p == 'f') {
			double f = __builtin_va_arg(args, double);
			serial2_send_number_double(f, 4);
		} else if (*p == '.') {
			p++;
			int precision = 0;
			while (*p >= '0' && *p <= '9') {
				precision = precision * 10 + (*p - '0');
				p++;
			}
			if (*p == 'f') {
				double f = __builtin_va_arg(args, double);
				serial2_send_number_double(f, precision);
			}
			if (*p == 'x') {
				uint64_t i = __builtin_va_arg(args, uint64_t);
				serial2_send_unsigned_number(i, 16, precision);
			}
		}
	}

	if (temp_index > 0) {
		put_into_buffer(temp_buffer, temp_index);
	}
}

void serial2_flush() {
	uint32_t tail = __atomic_load_n(&__buffer.tail, __ATOMIC_RELAXED);

	while (1) {
		uint32_t head =
		    __atomic_load_n(&__buffer.head, __ATOMIC_ACQUIRE);
		if (tail == head)
			break;

		if (!__atomic_compare_exchange_n(
		        &__buffer.tail, &tail, tail + 1, false,
		        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
			continue;
		}

		serial_entry_t* entry = &__buffer.buffer[tail % 8192];

		// BUG 1 FIX: spin tunggu produsen selesai menulis len
		// Jangan skip — slot sudah diklaim via CAS, harus diproses
		uint8_t len;
		while ((len = __atomic_load_n(&entry->len, __ATOMIC_ACQUIRE)) ==
		       0) {
			// cpu_relax() / __asm__ volatile ("pause") bisa
			// ditambahkan di sini
		}

		for (uint8_t i = 0; i < len; i++) {
			serial_putc(entry->data[i]);
		}

		__atomic_store_n(&entry->len, 0, __ATOMIC_RELEASE);
	}
}

static void parse_before_multicore(__builtin_va_list args, const char* fmt) {
	for (const char* p = fmt; *p != '\0'; p++) {
		if (*p != '%') {
			serial_putc(*p);
			continue;
		}
		p++;
		if (*p == 'd') {
			int i = __builtin_va_arg(args, int);
			serial_send_number(i, 10);
		} else if (*p == 'l') {
			p++;
			if (*p == 'u') {
				uint64_t i = __builtin_va_arg(args, uint64_t);
				serial_send_unsigned_number(i, 10, 0);
			} else if (*p == 'd') {
				int64_t i = __builtin_va_arg(args, int64_t);
				serial_send_number(i, 10);
			}
		} else if (*p == 'x') {
			uint64_t i = __builtin_va_arg(args, uint64_t);
			serial_send_number(i, 16);
		} else if (*p == 's') {
			char* s = __builtin_va_arg(args, char*);
			serial_send_string(s);
		} else if (*p == 'b') {
			uint64_t i = __builtin_va_arg(args, uint64_t);
			serial_send_number(i, 2);
		} else if (*p == 'B') {
			boolean_t i = __builtin_va_arg(args, boolean_t);
			if (i) {
				serial_send_string("true");
			} else {
				serial_send_string("false");
			}
		} else if (*p == 'f') {
			double f = __builtin_va_arg(args, double);
			serial_send_number_double(f, 4);
		} else if (*p == '.') {
			p++;
			int precision = 0;
			while (*p >= '0' && *p <= '9') {
				precision = precision * 10 + (*p - '0');
				p++;
			}
			if (*p == 'f') {
				double f = __builtin_va_arg(args, double);
				serial_send_number_double(f, precision);
			}
			if (*p == 'x') {
				uint64_t i = __builtin_va_arg(args, uint64_t);
				serial_send_unsigned_number(i, 16, precision);
			}
		}
	}
}

extern boolean_t multicore_start;

KERNEL_API void serial2_printf(const char* fmt, ...) {
	__builtin_va_list args;
	__builtin_va_start(args, fmt);
	parse_multicore(args, fmt);
	__builtin_va_end(args);
}

KERNEL_API void serial_printf(const char* fmt, ...) {
	__builtin_va_list args;
	__builtin_va_start(args, fmt);
	parse_before_multicore(args, fmt);
	__builtin_va_end(args);
}