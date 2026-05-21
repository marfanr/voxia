#include <libk/math.h>

uint64_t pow(uint64_t base, uint64_t exp) {
	uint64_t result = 1;
	for (uint64_t i = 0; i < exp; i++) {
		result *= base;
	}
	return result;
}

uint64_t clamp(uint64_t value, uint64_t min, uint64_t max) {
	if (value < min) {
		return min;
	} else if (value > max) {
		return max;
	} else {
		return value;
	}
}

uint64_t min(uint64_t a, uint64_t b) {
	return (a < b) ? a : b;
}
uint64_t max(uint64_t a, uint64_t b) {
	return (a > b) ? a : b;
}
int64_t abs(int64_t a) {
	return (a < 0) ? -a : a;
}
