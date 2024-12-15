
static void __syscall_3_(unsigned long syscall_num, unsigned long arg1,
                         unsigned long arg2) {
  asm("movq %0, %%rax;\n"
      "movq %1, %%rdi;\n"
      "movq %2, %%rsi;\n"
      "movq $0, %%rdx;\n" // Add this line to set rdx to 0
      "int $0x73"
      :
      : "r"(syscall_num), "r"(arg1), "r"(arg2)
      : "rax", "rdi", "rsi", "rdx"); // Add rdx to the clobber list
}

static unsigned long __syscall_4_(unsigned long syscall_num, unsigned long arg1,
                                  unsigned long arg2, unsigned long arg3) {
  unsigned long rax = 0;
  asm("movq %0, %%rax;\n"
      "movq %1, %%rdi;\n"
      "movq %2, %%rsi;\n"
      "movq %3, %%rdx;\n"
      "int $0x73"
      :
      : "r"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3)
      : "rax", "rdi", "rsi", "rdx");
  asm("movq %%rax, %0" : "=r"(rax));
  return rax;
}

static void __syscall_5_(unsigned long syscall_num, unsigned long arg1,
                         unsigned long arg2, unsigned long arg3,
                         unsigned long arg4) {
  asm("movq %0, %%rax;\n"
      "movq %1, %%rdi;\n"
      "movq %2, %%rsi;\n"
      "movq %3, %%rdx;\n"
      "movq %4, %%r10;\n"
      "int $0x73"
      :
      : "r"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4)
      : "rax", "rdi", "rsi", "rdx", "r10");
}

#define syscall(num, ...) __syscall_##num##_(__VA_ARGS__)

static const char *itoa(unsigned long value, int base) {
  static char result[256] = {
      0}; // Increase the size of the result array to accommodate larger numbers
  // check that the base if valid
  if (base < 2 || base > 36) {
    *result = '\0';
    return result;
  }

  char *ptr = result, *ptr1 = result, tmp_char;
  unsigned long tmp_value;

  do {
    tmp_value = value;
    value /= base;
    *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrst"
             "uvwxyz"[35 + (tmp_value - value * base)];
  } while (value);

  // Apply negative sign
  if (tmp_value < 0)
    *ptr++ = '-';
  *ptr-- = '\0';
  while (ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr-- = *ptr1;
    *ptr1++ = tmp_char;
  }
  return result;
}

unsigned long leng(char *buf) {
  unsigned long length = 0;
  while (*buf != '\0') {
    length++;
    buf++;
  }
  return length;
}

#define SYS_WRITE 0x1
#define SYS_ALLOC 0x8
#define CONSOLE_FILE_DESCRIPTOR 1

int main() {
  const char *str = "Hello from userspace\n";
  unsigned long len =
      syscall(4, SYS_WRITE, CONSOLE_FILE_DESCRIPTOR, (unsigned long)str, 21);
  if (len == 21) {
    const char *str2 = "Syscall success\n";
    syscall(4, SYS_WRITE, CONSOLE_FILE_DESCRIPTOR, (unsigned long)str2, 16);
  }

  unsigned long addr = syscall(4, SYS_ALLOC, 1, 0, 0);
  if (addr > 0) {
    const char *str2 = "alloc success at : ";
    syscall(4, SYS_WRITE, CONSOLE_FILE_DESCRIPTOR, (unsigned long)str2, 19);
    char *x = itoa(addr, 16);
    syscall(4, SYS_WRITE, CONSOLE_FILE_DESCRIPTOR, (unsigned long)x, leng(x));
  }
  return 0;
}
