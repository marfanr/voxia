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

// // aproksimasi cos(x) dalam radian
// uint64_t cos(uint64_t x) {
// 	uint64_t x2 = x * x;
// 	uint64_t result = 1.0;
// 	uint64_t term = 1.0;

// 	term *= -x2 / (2.0);
// 	result += term; // x^2 / 2!
// 	term *= -x2 / (3.0 * 4.0);
// 	result += term; // x^4 / 4!
// 	term *= -x2 / (5.0 * 6.0);
// 	result += term; // x^6 / 6!
// 	term *= -x2 / (7.0 * 8.0);
// 	result += term; // x^8 / 8!

// 	return result;
// }

// uint64_t sin(uint64_t x) {
// 	uint64_t x2 = x * x;
// 	uint64_t term = x;
// 	uint64_t result = term;

// 	term *= -x2 * x / (2.0 * 3.0);
// 	result += term; // x^3 / 3!
// 	term *= -x2 * x / (4.0 * 5.0);
// 	result += term; // x^5 / 5!
// 	term *= -x2 * x / (6.0 * 7.0);
// 	result += term; // x^7 / 7!

// 	return result;
// }

// uint64_t tan(uint64_t x) {
// 	return sin(x) / cos(x);
// }

// uint64_t asin(uint64_t x) {
// 	uint64_t x2 = x * x;
// 	uint64_t term = x;
// 	uint64_t result = term;

// 	term *= x2 * 1 / 6;
// 	result += term; // x^3
// 	term *= x2 * 3 / 20;
// 	result += term; // x^5
// 	term *= x2 * 5 / 28;
// 	result += term; // x^7

// 	return result;
// }

// uint64_t acos(uint64_t x) {
// 	return M_PI / 2 - asin(x);
// }

// uint64_t atan(uint64_t x) {
// 	uint64_t x2 = x * x;
// 	uint64_t term = x;
// 	uint64_t result = term;

// 	term *= x2 / 3;
// 	result += term; // x^3
// 	term *= -x2 / 5;
// 	result += term; // x^5
// 	term *= -x2 / 7;
// 	result += term; // x^7

// 	return result;
// }

// uint64_t fabs(uint64_t x) {
// 	return (x < 0) ? -x : x;
// }

// uint64_t sqrt(uint64_t x) {
// 	if (x < 0)
// 		return -1; // error untuk negatif
// 	if (x == 0)
// 		return 0;

// 	uint64_t result = x;
// 	uint64_t epsilon = 1e-12; // toleransi

// 	while (fabs(result * result - x) > epsilon) {
// 		result = 0.5 * (result + x / result);
// 	}

// 	return result;
// }

// float fmaxf(float a, float b) {
// 	return (a > b) ? a : b;
// }

// float fminf(float a, float b) {
// 	return (a < b) ? a : b;
// }

// float fmodf(float a, float b) {
// 	return a - (int) (a / b) * b;
// }

// float fabsf(float a) {
// 	return (a < 0) ? -a : a;
// }