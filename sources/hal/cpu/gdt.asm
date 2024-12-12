[bits 64]
global reloadGDT
section .text
reloadGDT:     
    push rdi
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    mov rax, rsi
    mov ds, rax
    mov es, rax
    mov fs, rax
    mov gs, rax
    mov ss, rax
    ret
