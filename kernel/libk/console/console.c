#include <console/console.h>
#include <graphic.h>
#include <libk/serial.h>
#include <memory/kalloc.h>
#include <spinlock.h>
#include <str.h>
#include <type.h>
#include <cpu/irq_lock.h>

#define SLOT_EMPTY 0x00
#define SLOT_WRITING 0xFE
#define SLOT_DROPPED 0xFF

#define CONSOLE_BUFFER_SIZE 2048
#define CONSOLE_BUFFER_MASK (CONSOLE_BUFFER_SIZE - 1)
#define SPIN_LIMIT 500000u

typedef struct {
	char data[128];
	uint32_t fg;
	uint8_t len;
} console_entry_t;

typedef struct {
	console_entry_t buffer[CONSOLE_BUFFER_SIZE];
	uint32_t head;
	uint32_t tail;
} console_ring_buffer_t;

static console_ring_buffer_t __console_buffer = {0};
static volatile unsigned char __console_flush_lock = 0;
static spinlock_t __console_producer_lock = {0};

static int pos_x = 0;
static int pos_y = 0;
static uint32_t fgcolor = 0xFFFFFFFF;

static void do_scroll(void) {
	int rows = (int)screen_rows();
	vxScroll(g_font_size, 0);
	pos_y = rows > 0 ? rows - 1 : 0;
	pos_x = 0;
}

static void advance_cursor(void) {
	pos_x++;
	if (pos_x >= (int)screen_cols()) {
		pos_x = 0;
		pos_y++;
	}
	if (pos_y >= (int)screen_rows())
		do_scroll();
}

static void put_char_raw(char c, uint32_t color) {
	if (c == '\n') {
		pos_x = 0;
		pos_y++;
		if (pos_y >= (int)screen_rows())
			do_scroll();
		return;
	}
	if (c == '\r') {
		pos_x = 0;
		return;
	}
	if (c == '\t') {
		int next = (pos_x + 8) & ~7;
		while (pos_x < next)
			advance_cursor();
		return;
	}
	putc(c, pos_x, pos_y, color, BLACK);
	advance_cursor();
}

static bool reserve_slot(uint32_t* out_idx) {
	uint32_t head, tail;
	for (;;) {
		head =
		    __atomic_load_n(&__console_buffer.head, __ATOMIC_RELAXED);
		tail =
		    __atomic_load_n(&__console_buffer.tail, __ATOMIC_ACQUIRE);

		if ((head - tail) >= CONSOLE_BUFFER_SIZE)
			return false;

		if (__atomic_compare_exchange_n(
		        &__console_buffer.head, &head, head + 1, true,
		        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
			break;
		__asm__ volatile("pause");
	}
	*out_idx = head & CONSOLE_BUFFER_MASK;
	return true;
}

static void put_into_buffer(const char* str, uint8_t len, uint32_t color) {
	uint32_t idx;
	if (!reserve_slot(&idx))
		return;

	console_entry_t* entry = &__console_buffer.buffer[idx];

	uint32_t spin = 0;
	while (__atomic_load_n(&entry->len, __ATOMIC_ACQUIRE) != SLOT_EMPTY) {
		if (++spin >= SPIN_LIMIT) {
			__atomic_store_n(&entry->len, SLOT_DROPPED,
			                 __ATOMIC_RELEASE);
			return;
		}
		__asm__ volatile("pause");
	}

	__atomic_store_n(&entry->len, SLOT_WRITING, __ATOMIC_RELEASE);
	entry->fg = color;

	if (len > 128)
		len = 128;
	for (uint8_t i = 0; i < len; i++)
		entry->data[i] = str[i];

	__atomic_store_n(&entry->len, len, __ATOMIC_RELEASE);
}

static void console_flush(void) {
	if (__atomic_test_and_set(&__console_flush_lock, __ATOMIC_ACQUIRE))
		return;

	for (;;) {
		uint32_t tail =
		    __atomic_load_n(&__console_buffer.tail, __ATOMIC_RELAXED);
		uint32_t head =
		    __atomic_load_n(&__console_buffer.head, __ATOMIC_ACQUIRE);

		if (tail == head)
			break;

		console_entry_t* entry =
		    &__console_buffer.buffer[tail & CONSOLE_BUFFER_MASK];

		uint32_t spin = 0;
		uint8_t len;
		for (;;) {
			len = __atomic_load_n(&entry->len, __ATOMIC_ACQUIRE);
			if (len != SLOT_EMPTY && len != SLOT_WRITING)
				break;
			if (len == SLOT_EMPTY)
				break;
			if (++spin >= SPIN_LIMIT)
				break;
			__asm__ volatile("pause");
		}

		if (len != SLOT_EMPTY && len != SLOT_WRITING &&
		    len != SLOT_DROPPED) {
			for (uint8_t i = 0; i < len; i++) {
				put_char_raw(entry->data[i], entry->fg);
			}
		}

		__atomic_store_n(&entry->len, SLOT_EMPTY, __ATOMIC_RELEASE);
		__atomic_store_n(&__console_buffer.tail, tail + 1,
		                 __ATOMIC_RELEASE);
	}

	__atomic_clear(&__console_flush_lock, __ATOMIC_RELEASE);
}

int console_get_pos_x(void) { return pos_x; }
int console_get_pos_y(void) { return pos_y; }

void console_println(const char* str) {
	size_t len = strlen(str);
	put_into_buffer(str, (uint8_t)len, fgcolor);
	put_into_buffer("\n", 1, fgcolor);
	console_flush();
}

void console_print(const char* str, uint64_t len) {
	while (len > 0) {
		uint8_t chunk = (len > 128) ? 128 : (uint8_t)len;
		put_into_buffer(str, chunk, fgcolor);
		str += chunk;
		len -= chunk;
	}
	console_flush();
}

static char* val_to_str(uint64_t val, uint64_t base) {
	char* out = (char*)kalloc(65);
	if (!out)
		return NULL;

	if (val == 0) {
		out[0] = '0';
		out[1] = '\0';
		return out;
	}

	char buf[65];
	int i = 0, j = 0;
	const char* digits = "0123456789ABCDEF";
	while (val > 0) {
		buf[i++] = digits[val % base];
		val /= base;
	}

	for (i = i - 1; i >= 0; i--)
		out[j++] = buf[i];
	out[j] = '\0';
	return out;
}

static void vprintf_internal(const char* fmt, __builtin_va_list args) {
	char temp[128];
	int tidx = 0;

#define FLUSH_TEMP()                                                           \
	if (tidx > 0) {                                                        \
		put_into_buffer(temp, (uint8_t)tidx, fgcolor);                 \
		tidx = 0;                                                      \
	}

	while (*fmt) {
		if (*fmt == '%') {
			FLUSH_TEMP();
			fmt++;
			if (*fmt == '\0') {
				break;
			}
			switch (*fmt) {
			case 's': {
				char* s = __builtin_va_arg(args, char*);
				if (!s)
					s = "(null)";
				size_t slen = strlen(s);
				while (slen > 0) {
					uint8_t c =
					    (slen > 128) ? 128 : (uint8_t)slen;
					put_into_buffer(s, c, fgcolor);
					s += c;
					slen -= c;
				}
				break;
			}
			case 'd': {
				int64_t n = __builtin_va_arg(args, int64_t);
				if (n < 0) {
					put_into_buffer("-", 1, fgcolor);
					n = -n;
				}
				char* s = val_to_str((uint64_t)n, 10);
				if (s) {
					put_into_buffer(s, (uint8_t)strlen(s),
					                fgcolor);
					kfree2(s);
				}
				break;
			}
			case 'u': {
				char* s = val_to_str(
				    __builtin_va_arg(args, uint64_t), 10);
				if (s) {
					put_into_buffer(s, (uint8_t)strlen(s),
					                fgcolor);
					kfree2(s);
				}
				break;
			}
			case 'x': {
				char* s = val_to_str(
				    __builtin_va_arg(args, uint64_t), 16);
				if (s) {
					put_into_buffer(s, (uint8_t)strlen(s),
					                fgcolor);
					kfree2(s);
				}
				break;
			}
			case 'b': {
				char* s = val_to_str(
				    __builtin_va_arg(args, uint64_t), 2);
				if (s) {
					put_into_buffer(s, (uint8_t)strlen(s),
					                fgcolor);
					kfree2(s);
				}
				break;
			}
			case 'c': {
				char c = (char)__builtin_va_arg(args, int);
				put_into_buffer(&c, 1, fgcolor);
				break;
			}
			case '%':
				put_into_buffer("%", 1, fgcolor);
				break;
			default:
				put_into_buffer("%", 1, fgcolor);
				put_into_buffer(fmt, 1, fgcolor);
				break;
			}
		} else {
			temp[tidx++] = *fmt;
			if (tidx == 128)
				FLUSH_TEMP();
		}
		fmt++;
	}
	FLUSH_TEMP();
}


void console_printf(const char* fmt, ...) {
	uintptr_t flags = irq_save();
	spin_acquire(&__console_producer_lock);

	__builtin_va_list args;
	__builtin_va_start(args, fmt);
	vprintf_internal(fmt, args);
	__builtin_va_end(args);
	console_flush();

	spin_release(&__console_producer_lock);
	irq_restore(flags);
}

void console_vaprintf(const char* fmt, __builtin_va_list args) {
	uintptr_t flags = irq_save();
	spin_acquire(&__console_producer_lock);

	vprintf_internal(fmt, args);
	console_flush();

	spin_release(&__console_producer_lock);
	irq_restore(flags);
}

void console_newline(void) {
	uintptr_t flags = irq_save();
	spin_acquire(&__console_producer_lock);

	put_into_buffer("\n", 1, fgcolor);
	console_flush();

	spin_release(&__console_producer_lock);
	irq_restore(flags);
}

void console_chfg(uint32_t color) { fgcolor = color; }

void console_add_space(int n) {
	uintptr_t flags = irq_save();
	spin_acquire(&__console_producer_lock);

	while (n > 0) {
		int c = (n > 128) ? 128 : n;
		char spaces[128];
		for (int i = 0; i < c; i++)
			spaces[i] = ' ';
		put_into_buffer(spaces, (uint8_t)c, fgcolor);
		n -= c;
	}
	console_flush();

	spin_release(&__console_producer_lock);
	irq_restore(flags);
}

void console_set_pos(int x, int y) {
	pos_x = x;
	pos_y = y;
}
