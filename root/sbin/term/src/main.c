#include "term.h"

extern int main() {
	int fd = open("/dev/event/event0", 0, 0);
	char *c = (char *)"#> | ";
	write(0, c, 3);
	// (void)argc;
	// (void)argv;

	// asm ("int $20");
	// asm volatile("cli");

	char buff[100];

	// while (1) {
	int a = read(0, buff, 10);
	// }

	for (;;)
		;
	return 3;
}
