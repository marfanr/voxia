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

global syscall_entry
extern syscall_dispatch

syscall_entry:
    swapgs
    mov [gs:0x18], rsp
    mov rsp, [gs:0x10]

    ; IRET frame
    push qword 0x43         ; SS
    push qword [gs:0x18]    ; RSP
    push r11                ; RFLAGS
    push qword 0x4B         ; CS
    push rcx                ; RIP

    push qword 0            ; err_code
    push qword 0x80         ; int_no (Syscall)

    ; pushall

    ; mov rdi, rsp
    call syscall_dispatch

    ; popall
    add rsp, 16             ; int_no, err_code

    swapgs
    ; for now using iretq
    ; TODO: setup propper gs so can be used to make a hot path 
    iretq
