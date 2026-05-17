#include "serial.h"
#include "libk/type.h"
#include <init/init.h>
#include <libk/io.h>
#include <str.h>

#define INT_ENABLE_OFFSET 1
#define SERIAL_BUFFER2_SIZE (4096 * 3)
#define SERIAL_BUFFER2_MASK (SERIAL_BUFFER2_SIZE - 1)

/* Nilai sentinel: slot sedang ditulis oleh producer */
#define SLOT_WRITING 0xFE
/* Nilai sentinel: slot sengaja dibuang (drop) */
#define SLOT_DROPPED 0xFF
/* Nilai sentinel: slot kosong / sudah dikonsumsi */
#define SLOT_EMPTY 0x00

/* Batas spin sebelum menyerah (± 1 ms pada 1 GHz) */
#define SPIN_LIMIT 500000u

typedef struct {
	char data[128];
	uint8_t len; /* SLOT_EMPTY / SLOT_WRITING / SLOT_DROPPED / 1-128 */
} serial_entry_t;

typedef struct {
	serial_entry_t buffer[SERIAL_BUFFER2_SIZE];
	uint32_t head; /* next slot yang akan di-claim producer */
	uint32_t tail; /* next slot yang akan dikonsumsi consumer */
} serial_ring_buffer_t;

static serial_ring_buffer_t __buffer = {0};

/*
 * FIX: flush_owner memastikan hanya SATU core yang flush pada satu waktu.
 * Menggunakan __atomic_test_and_set / __atomic_clear (spinlock 1-bit).
 */
static volatile unsigned char __flush_lock = 0;

/* ------------------------------------------------------------------ */
/*  Hardware setup                                                      */
/* ------------------------------------------------------------------ */

void serial_setup(void) {
	outb(SERIAL_COM1 + INT_ENABLE_OFFSET, 0x00);
	outb(SERIAL_COM1 + 3, 0x80);
	outb(SERIAL_COM1 + 0, 0x03);
	outb(SERIAL_COM1 + INT_ENABLE_OFFSET, 0x00);
	outb(SERIAL_COM1 + 3, 0x03);
	outb(SERIAL_COM1 + 2, 0xC7);
	outb(SERIAL_COM1 + 4, 0x0B);
}

int serial_is_transmit_empty(void) {
	return inb(SERIAL_COM1 + 5) & 0x20;
}

extern void serial_putc(char c) {
	while (serial_is_transmit_empty() == 0)
		__asm__ volatile("pause");

	outb(SERIAL_COM1, (uint8_t) c);
}

void serial_send_string(char* str) {
	for (int i = 0; str[i] != '\0'; i++)
		serial_putc(str[i]);
}

void serial_send_number(int64_t num, int base) {
	if (base < 2 || base > 16)
		return;

	const char* digits = "0123456789ABCDEF";
	char buf[65];
	int i = 0;
	bool negative = false;

	uint64_t n;
	if (base == 10 && num < 0) {
		negative = true;
		n = (uint64_t) (-num);
	} else {
		n = (uint64_t) num;
	}

	if (n == 0) {
		buf[i++] = '0';
	} else {
		while (n > 0) {
			buf[i++] = digits[n % (uint64_t) base];
			n /= (uint64_t) base;
		}
	}

	if (negative)
		buf[i++] = '-';

	for (int j = i - 1; j >= 0; j--)
		serial_putc(buf[j]);
}

void serial_clear(void) {
	serial_printf("\033[2J\033[H");
}

/* ------------------------------------------------------------------ */
/*  Ring-buffer internals                                               */
/* ------------------------------------------------------------------ */

/*
 * reserve_slot: klaim satu slot di ring-buffer secara atomik.
 *
 * FIX 1: tambah __asm__ volatile("pause") di CAS loop agar tidak
 *         thrash pipeline CPU pada 8+ core.
 * FIX 2: perbandingan (head - tail) tetap aman karena unsigned wraparound,
 *         tapi sekarang kita juga re-load tail di setiap iterasi agar
 *         tidak stuck ketika consumer maju.
 */
static bool reserve_slot(uint32_t* out_idx) {
	uint32_t head, tail;

	for (;;) {
		head = __atomic_load_n(&__buffer.head, __ATOMIC_RELAXED);
		tail = __atomic_load_n(&__buffer.tail, __ATOMIC_ACQUIRE);

		if ((head - tail) >= SERIAL_BUFFER2_SIZE)
			return false; /* buffer penuh, buang pesan */

		if (__atomic_compare_exchange_n(&__buffer.head, &head, head + 1,
						true, __ATOMIC_ACQ_REL,
						__ATOMIC_RELAXED))
			break;

		/* FIX: hint ke CPU agar pipeline tidak thrash */
		__asm__ volatile("pause");
	}

	*out_idx = head & SERIAL_BUFFER2_MASK;
	return true;
}

/*
 * put_into_buffer: tulis string ke slot yang sudah di-claim.
 *
 * FIX 3: ganti infinite busy-wait dengan spin ber-batas + SLOT_DROPPED.
 *         Kalau setelah SPIN_LIMIT iterasi slot masih belum bebas
 *         (artinya consumer sangat lambat atau macet), kita tandai slot
 *         sebagai DROPPED supaya consumer bisa skip dan tidak hang.
 *
 * FIX 4: tandai SLOT_WRITING segera setelah claim sehingga consumer tahu
 *         slot ini sedang diisi dan jangan di-skip sebelum waktunya.
 */
static void put_into_buffer(const char* str, uint8_t len) {
	uint32_t idx;
	if (!reserve_slot(&idx))
		return;

	serial_entry_t* entry = &__buffer.buffer[idx];

	/*
	 * Tunggu sampai consumer selesai mengosongkan slot ini.
	 * Slot bisa masih terisi kalau ring buffer wrap-around dan
	 * consumer belum sampai ke sini.
	 */
	uint32_t spin = 0;
	while (__atomic_load_n(&entry->len, __ATOMIC_ACQUIRE) != SLOT_EMPTY) {
		if (++spin >= SPIN_LIMIT) {
			/*
			 * Consumer macet atau sangat lambat.
			 * Tandai slot sebagai DROPPED agar head/tail tetap
			 * konsisten dan core lain tidak ikut macet.
			 */
			__atomic_store_n(&entry->len, SLOT_DROPPED,
					 __ATOMIC_RELEASE);
			return;
		}
		__asm__ volatile("pause");
	}

	/* Tandai "sedang ditulis" — consumer akan skip sampai len berubah */
	__atomic_store_n(&entry->len, SLOT_WRITING, __ATOMIC_RELEASE);

	/* Salin data */
	if (len > (uint8_t) sizeof(entry->data))
		len = (uint8_t) sizeof(entry->data);

	for (uint8_t i = 0; i < len; i++)
		entry->data[i] = str[i];

	/* Publish ke consumer */
	__atomic_store_n(&entry->len, len, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------------ */
/*  Buffered number/string helpers (multicore path)                    */
/* ------------------------------------------------------------------ */

static void serial2_send_number(int64_t num, int base) {
	const char* digits = "0123456789ABCDEF";
	char buf[65];
	int i = 0;
	bool negative = false;

	if (base == 10 && num < 0) {
		negative = true;
		num = -num;
	}

	uint64_t n = (uint64_t) num;

	if (n == 0) {
		buf[i++] = '0';
	} else {
		while (n > 0 && i < 64) {
			buf[i++] = digits[n % (uint64_t) base];
			n /= (uint64_t) base;
		}
	}

	if (negative && i < 64)
		buf[i++] = '-';

	/* balik urutan */
	char buf2[65];
	__builtin_memset(buf2, 0, sizeof(buf2));
	for (int j = i - 1; j >= 0; j--)
		buf2[i - j - 1] = buf[j];

	put_into_buffer(buf2, (uint8_t) i);
}

static void serial2_send_unsigned_number(uint64_t num, int base, int limit) {
	const char* digits = "0123456789ABCDEF";
	char buf[65];
	int i = 0;

	if (base < 2 || base > 16) {
		put_into_buffer("?", 1);
		return;
	}

	uint64_t n = num;

	if (n == 0) {
		buf[i++] = '0';
	} else {
		while (n > 0 && i < 64) {
			buf[i++] = digits[n % (uint64_t) base];
			n /= (uint64_t) base;
		}
	}

	int count = (limit > 0 && limit < i) ? limit : i;
	int start = i - count;
	int end = i - 1;

	char buf2[65] = {0};
	for (int j = end; j >= start; j--)
		buf2[end - j] = buf[j];

	put_into_buffer(buf2, (uint8_t) count);
}

/* ------------------------------------------------------------------ */
/*  Format parsers                                                      */
/* ------------------------------------------------------------------ */

static void parse_multicore(__builtin_va_list args, const char* fmt) {
	char temp_buffer[128];
	uint8_t temp_index = 0;

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
			serial2_send_number((int64_t) i, 10);
		} else if (*p == 'l') {
			p++;
			if (*p == 'u') {
				uint64_t i = __builtin_va_arg(args, uint64_t);
				serial2_send_unsigned_number(i, 10, 0);
			} else if (*p == 'd') {
				int64_t i = __builtin_va_arg(args, int64_t);
				serial2_send_number(i, 10);
			}
		} else if (*p == 'p') {
			uintptr_t ptr = __builtin_va_arg(args, uintptr_t);
			serial2_send_unsigned_number((uint64_t) ptr, 16, 0);
		} else if (*p == 'x') {
			uint64_t i = __builtin_va_arg(args, uint64_t);
			serial2_send_unsigned_number(i, 16, 0);
		} else if (*p == 's') {
			char* s = __builtin_va_arg(args, char*);
			if (!s) {
				put_into_buffer("(null)", 6);
			} else {
				size_t slen = strlen(s);
				uint8_t blen =
					(slen > 128) ? 128 : (uint8_t) slen;
				put_into_buffer(s, blen);
			}
		} else if (*p == 'b') {
			uint64_t i = __builtin_va_arg(args, uint64_t);
			serial2_send_unsigned_number(i, 2, 0);
		} else if (*p == 'B') {
			int i = __builtin_va_arg(args, int);
			if ((boolean_t) i)
				put_into_buffer("True", 4);
			else
				put_into_buffer("False", 5);
		} else if (*p == '.') {
			p++;
			int precision = 0;
			while (*p >= '0' && *p <= '9') {
				precision = precision * 10 + (*p - '0');
				p++;
			}
			if (*p == 'x') {
				uint64_t i = __builtin_va_arg(args, uint64_t);
				serial2_send_unsigned_number(i, 16, precision);
			}
		}
	}

	if (temp_index > 0)
		put_into_buffer(temp_buffer, temp_index);
}

/* ------------------------------------------------------------------ */
/*  Flush — hanya boleh efektif dari 1 core pada satu waktu            */
/* ------------------------------------------------------------------ */

/*
 * FIX 5: __flush_lock memastikan hanya satu core yang mengonsumsi
 *         tail pada satu waktu.  Core lain yang memanggil flush
 *         langsung return — mereka tidak perlu flush karena core
 *         pemenang akan menguras semua entry yang sudah siap.
 *
 * FIX 6: consumer sekarang handle SLOT_WRITING dengan spin ber-batas
 *         (bukan infinite) dan handle SLOT_DROPPED dengan skip.
 */
void serial2_flush(void) {
	/* Coba ambil lock; kalau gagal, core lain sedang flush — tidak apa. */
	if (__atomic_test_and_set(&__flush_lock, __ATOMIC_ACQUIRE))
		return;

	for (;;) {
		uint32_t tail =
			__atomic_load_n(&__buffer.tail, __ATOMIC_RELAXED);
		uint32_t head =
			__atomic_load_n(&__buffer.head, __ATOMIC_ACQUIRE);

		if (tail == head)
			break; /* buffer kosong */

		serial_entry_t* entry =
			&__buffer.buffer[tail & SERIAL_BUFFER2_MASK];

		/* Tunggu producer selesai menulis, tapi dengan batas spin */
		uint32_t spin = 0;
		uint8_t len;
		for (;;) {
			len = __atomic_load_n(&entry->len, __ATOMIC_ACQUIRE);
			if (len != SLOT_EMPTY && len != SLOT_WRITING)
				break; /* DROPPED atau data valid */
			if (len == SLOT_EMPTY)
				break; /* belum ada produsen sama sekali — unusual */
			if (++spin >= SPIN_LIMIT)
				break; /* producer macet, skip saja */
			__asm__ volatile("pause");
		}

		/* Kirim ke hardware kalau bukan sentinel */
		if (len != SLOT_EMPTY && len != SLOT_WRITING
		    && len != SLOT_DROPPED) {
			for (uint8_t i = 0; i < len; i++)
				serial_putc(entry->data[i]);
		}

		/* Bebaskan slot */
		__atomic_store_n(&entry->len, SLOT_EMPTY, __ATOMIC_RELEASE);

		/* Maju tail — hanya kita yang pegang lock, jadi tidak perlu CAS */
		__atomic_store_n(&__buffer.tail, tail + 1, __ATOMIC_RELEASE);
	}

	__atomic_clear(&__flush_lock, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------------ */
/*  Direct (pre-multicore) path                                         */
/* ------------------------------------------------------------------ */

static void serial_send_padded(uint64_t val, int base, int width, char pad) {
	const char* digits = "0123456789ABCDEF";
	char buf[65];
	int i = 0;

	if (val == 0) {
		buf[i++] = '0';
	} else {
		uint64_t n = val;
		while (n > 0) {
			buf[i++] = digits[n % (uint64_t) base];
			n /= (uint64_t) base;
		}
	}

	for (int p = i; p < width; p++)
		serial_putc(pad);

	for (int j = i - 1; j >= 0; j--)
		serial_putc(buf[j]);
}

static void parse_before_multicore(__builtin_va_list args, const char* fmt) {
	for (const char* p = fmt; *p != '\0'; p++) {
		if (*p != '%') {
			serial_putc(*p);
			continue;
		}
		p++;

		char pad = ' ';
		if (*p == '0') {
			pad = '0';
			p++;
		}

		int width = 0;
		while (*p >= '0' && *p <= '9') {
			width = width * 10 + (*p - '0');
			p++;
		}

		int precision = 4;
		if (*p == '.') {
			p++;
			precision = 0;
			while (*p >= '0' && *p <= '9') {
				precision = precision * 10 + (*p - '0');
				p++;
			}
		}
		(void) precision;

		bool is_long = false;
		if (*p == 'l') {
			is_long = true;
			p++;
		}

		switch (*p) {
		case 'd': {
			int64_t i =
				is_long ? __builtin_va_arg(args, int64_t)
					: (int64_t) __builtin_va_arg(args, int);
			if (i < 0) {
				serial_putc('-');
				serial_send_padded((uint64_t) (-i), 10,
						   width ? width - 1 : 0, pad);
			} else {
				serial_send_padded((uint64_t) i, 10, width,
						   pad);
			}
			break;
		}
		case 'u': {
			uint64_t i = is_long ? __builtin_va_arg(args, uint64_t)
					     : (uint64_t) __builtin_va_arg(
						       args, unsigned int);
			serial_send_padded(i, 10, width, pad);
			break;
		}
		case 'x': {
			uint64_t i = is_long ? __builtin_va_arg(args, uint64_t)
					     : (uint64_t) __builtin_va_arg(
						       args, unsigned int);
			serial_send_padded(i, 16, width, pad);
			break;
		}
		case 'b': {
			uint64_t i = is_long ? __builtin_va_arg(args, uint64_t)
					     : (uint64_t) __builtin_va_arg(
						       args, unsigned int);
			serial_send_padded(i, 2, width, pad);
			break;
		}
		case 's': {
			char* s = __builtin_va_arg(args, char*);
			if (!s)
				s = "(null)";
			serial_send_string(s);
			break;
		}
		case 'B': {
			int i = __builtin_va_arg(args, int);
			serial_send_string(i ? "true" : "false");
			break;
		}
		case 'c': {
			char c = (char) __builtin_va_arg(args, int);
			serial_putc(c);
			break;
		}
		case '%':
			serial_putc('%');
			break;
		default:
			serial_putc('%');
			serial_putc(*p);
			break;
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

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