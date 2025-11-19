#ifndef __HAL__CPU__REGISTER_H__
#define __HAL__CPU__REGISTER_H__

#include <libk/type.h>

typedef struct
{
    uint8_t fpu_state[512] __attribute__((aligned(16)));
    uint64_t rax, rbx, rcx, rdx, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, int_no,
        err_code, rip, cs, rflags, rsp, ss;
} __attribute__((packed)) cpu_register_t;

#endif // __HAL__CPU__REGISTER_H__