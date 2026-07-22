; context_switch.asm
global kernel_context_save
global kernel_context_restore

; kernel_context_save(uintptr_t* save_rsp, uintptr_t scheduler_rsp, uintptr_t entry_point)
; rdi = &thread->kernel_rsp
; rsi = scheduler stack RSP
; rdx = entry point (scheduler_resume_point)
kernel_context_save:
    ; save all callee param + return address
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq

    mov [rdi], rsp
    mov rsp, rsi
    mov [rsp], rdx
    ret

; kernel_context_restore(uintptr_t kernel_rsp)
; rdi = thread->kernel_rsp yang mau di-resume
kernel_context_restore:
    mov rsp, rdi

    pop rax
    and rax, 0xFFFFFEFF
    push rax

    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret