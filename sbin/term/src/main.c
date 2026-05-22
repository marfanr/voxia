#include "term.h"

extern int main() {
	// (void)argc;
	// (void)argv;

	// asm ("int $20");
	// asm volatile("cli");
	
	char buff[100];
	int a = read(0, buff, 10);
	
	char *c = (char *)"hello world";
	write(0, c, a);
	
	for (;;)
		;
	return 3;
}
