
%macro pushall 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15    
%endmacro

%macro popall 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

extern interrupt_handler
int_common:
    mov qword [saved_rax], rax
    cmp qword [rsp + 24], 0x28
    je .next
    swapgs
    .next:
        pushall
        mov rdi, rsp
        call interrupt_handler
        popall
        add rsp, 16
        cmp qword [rsp + 8], 0x28
        je .next1
        swapgs
    .next1:
        iretq


%macro isr 1
isr_%1:
%if !(%1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17 || %1 == 21 || %1 == 29 || %1 == 30)
    push 0
%endif
    push %1
    jmp int_common
%endmacro

%assign i 0
%rep 256
isr i
%assign i i+1
%endrep

[global int_table]
section .data
int_table:
%assign i 0
%rep 256
    dq isr_%+i
%assign i i+1
%endrep

extern syscall
extern apic_eoi
global syscall_interupt
extern serial_send_number
extern serial_send_string
syscall_interupt:        
        pushall
        ; mov rbp, rsp
        ; save rax and rdi to param
        mov qword [syscall_param], rax       
        mov qword [syscall_param + 8], rdi
        mov qword [syscall_param + 16], rsi
        mov qword [syscall_param + 24], rdx
        mov qword [syscall_param + 32], rcx
        mov qword [syscall_param + 40], r8
        mov qword [syscall_param + 48], r9

        ; assign syscall_param addr as syscall_param 1 on c function
        mov rdi, syscall_param
        call syscall 
        mov qword [syscall_ret], rax

        popall

        mov rax, [syscall_ret]
        iretq



section .bss
align 16
global saved_rax
saved_rax resq 1
syscall_param resq 6
syscall_ret resq 1