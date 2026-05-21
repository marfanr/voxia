__attribute__((unused))
static __inline unsigned long __syscall3(long n, long a1, long a2, long a3)
{
	unsigned long ret;
	__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
						  "d"(a3) : "rcx", "r11", "memory");
	return ret;
}

extern "C" int main() {
	// (void)argc;
	// (void)argv;

	// asm ("int $20");
	// asm volatile("cli");
	__syscall3(10, 0x2, 0x3, 0x4);
	for (;;)
		;
	return 3;
}
