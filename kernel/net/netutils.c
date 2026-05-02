#include "netutils.h"

uint32_t vxInetAddr(const char* addr) {
	uint32_t ip = 0;
	uint8_t* b = (uint8_t*) &ip;

	for (int i = 0; i < 4; i++) {
		uint32_t octet = 0;
		while (*addr >= '0' && *addr <= '9') {
			octet = (octet * 10)
				+ (*addr
				   - '0'); // Konversi karakter ASCII ke integer
			addr++; // Geser pointer string ke karakter berikutnya
		}
		b[i] = (uint8_t) octet;
		if (*addr == '.') {
			addr++;
		} else if (*addr == '\0' && i < 3) {
			return 0; // Atau return error code khusus Anda
		}
	}

	return ip;
}

uint16_t checksum16(const uint16_t* data, size_t length) {
	uint32_t sum = 0;

	// jumlahkan per 16-bit
	while (length > 1) {
		sum += *data++;
		length -= 2;
	}

	// kalau ada sisa 1 byte
	if (length > 0) {
		sum += *((uint8_t*) data);
	}

	// fold 32-bit ke 16-bit
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	// one's complement
	return (uint16_t) (~sum);
}

uint16_t checksum16_adc(const uint16_t* data, size_t length) {
	uint64_t sum = 0;

	while (length >= 2) {
		__asm__ volatile("addw (%1), %w0\n\t"
				 "adcq $0, %0\n\t"
				 : "+r"(sum)
				 : "r"(data)
				 : "memory");
		data++;
		length -= 2;
	}

	if (length) {
		sum += *(uint8_t*) data;
	}

	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);

	return ~sum;
}

static int u8_to_str(uint8_t val, char* buf) {
	int i = 0;

	if (val >= 100) {
		buf[i++] = '0' + (val / 100);
		val %= 100;
		buf[i++] = '0' + (val / 10);
		buf[i++] = '0' + (val % 10);
	} else if (val >= 10) {
		buf[i++] = '0' + (val / 10);
		buf[i++] = '0' + (val % 10);
	} else {
		buf[i++] = '0' + val;
	}

	return i;
}

char* vxInetNtoa(uint32_t ip, char* buffer) {
	uint8_t* b = (uint8_t*) &ip;
	int len = 0;

	for (int i = 0; i < 4; i++) {
		len += u8_to_str(b[i], buffer + len);
		if (i < 3)
			buffer[len++] = '.';
	}

	buffer[len] = '\0';
	return buffer;
}