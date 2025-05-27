global user_switch

user_switch:
    mov rsp, [rdi + 48] ; skip over the pushed rdi
    
    mov ax, (0x40) | 3        
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax               

    ; Set up stack frame untuk IRET    
    mov rax, rsp       ; Alamat stack user (pre-allocated)
    push (0x40) | 3           ; Ring 3 data segment selector
    push rax                  ; ESP (stack pointer untuk user mode)
    pushf                     ; EFLAGS (ambil dari state saat ini)
    push (0x38) | 3           ; Ring 3 code segment selector
    mov rax, [rdi + 64]
    push rax                  ; Alamat program user (RIP dari RDI)

    iretq                     ; Switch ke user mode