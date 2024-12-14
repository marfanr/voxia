
void _syscall(unsigned long func, unsigned long arg1, unsigned long arg2,
              unsigned long arg3) {
  asm volatile("movq %0, %%rax\n"
               "movq %1, %%rdi\n"
               "movq %2, %%rsi\n"
               "movq %3, %%rdx\n"
               "int $0x73"
               :
               : "r"(func), "r"(arg1), "r"(arg2), "r"(arg3)
               : "memory");
}
#define SYSCALL(VA_ARGS...) _syscall(VA_ARGS)
void hello() {
  char *txt = "Hello from\n";
  SYSCALL(0x1, 1, txt, 11);
}

int main() {
  // hello();
  char *txt = "#>\n";
  SYSCALL(0x1, 1, txt, 3);
  txt = "Hello from userspace\n";
  SYSCALL(0x1, 1, txt, 21);
  // syscall(0x2, 1, txt, 21);
  hello();
  // asm("add %%rsp, 0x8" : : : "memory");
  // for (;;)
  //   ;
  return 1;
}
