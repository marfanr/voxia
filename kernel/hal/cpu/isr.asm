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

extern vxInterruptHandler

; Stack layout at int_common entry:
;   [rsp+ 0] = error code   (pushed by isr macro or CPU)
;   [rsp+ 8] = int number   (pushed by isr macro)
;   [rsp+16] = RIP          (pushed by CPU)
;   [rsp+24] = CS           (pushed by CPU)  <-- ring check
;   [rsp+32] = RFLAGS
;   [rsp+40] = RSP          (only on privilege change)
;   [rsp+48] = SS           (only on privilege change)

int_common:
    ; CS is at [rsp+24] before pushall
    test qword [rsp+24], 3
    jz .skip_swapgs_entry
    swapgs
.skip_swapgs_entry:

    pushall
    ; After pushall: 15 regs * 8 = 120 bytes pushed
    ; CS is now at [rsp + 120 + 24] = [rsp+144]

    mov rbx, rsp            ; rbx = pointer to saved-regs frame (interrupt_stack_frame_t)

    ; Disable x87 EM + TS so fxsave doesn't #NM
    mov rax, cr0
    and rax, ~((1 << 2) | (1 << 3))
    mov cr0, rax

    ; Allocate 512-byte fxsave buffer, 64-byte aligned (safe for fxsave/xsave)
    sub rsp, 512
    and rsp, -64
    fxsave [rsp]

    mov rdi, rbx            ; arg1: interrupt_stack_frame_t*
    mov rsi, rsp            ; arg2: fxsave area* (optional, can be ignored in handler)
    call vxInterruptHandler

    fxrstor [rsp]           ; restore FPU — rsp unchanged since fxsave

    mov rsp, rbx            ; restore to post-pushall stack position

    popall

    ; CS is at [rsp+24] again (same layout as entry, before pushall)
    test qword [rsp+24], 3
    jz .skip_swapgs_exit
    swapgs
.skip_swapgs_exit:
    add rsp, 16             ; discard error code + int number
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

global int_table
section .data
int_table:
%assign i 0
%rep 256
    dq isr_%+i
%assign i i+1
%endrep