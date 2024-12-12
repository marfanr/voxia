
void syscall(unsigned long func, unsigned long arg1, unsigned long arg2,
             unsigned long arg3) {
  asm volatile("mov %0, %%rdi" : : "r"(arg1));
  asm volatile("mov %0, %%rsi" : : "r"(arg2));
  asm volatile("mov %0, %%rdx" : : "r"(arg3));
  asm volatile("mov %0, %%rax" : : "r"(func));
  asm volatile("int $0x73");
}
void hello() {
  char *txt = "Hello from userspace\n";
  syscall(0x1, 1, txt, 21);
}

int main() {
  char *txt = "#> ";
  syscall(0x1, 1, txt, 21);
  txt = "Hello from userspace\n";
  syscall(0x1, 1, txt, 21);
  syscall(0x1, 1, txt, 21);
  // asm("add %%rsp, 0x8" : : : "memory");
  // for (;;)
  //   ;
  return 1;
}
