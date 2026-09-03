global __fast_memset__
__fast_memset__:
    mov rax, rsi
    movzx rsi, al       ; broadcast byte to 64-bit
    mov ah, al
    mov r8w, ax
    shl rax, 16
    mov ax, r8w
    mov r8d, eax
    shl rax, 32
    or rax, r8
    mov rcx, rdx
    shr rcx, 3
    rep stosq
    mov rcx, rdx
    and rcx, 7
    rep stosb
    ret

global __fast__memcpy__
__fast__memcpy__:
    mov rcx, rdx
    shr rcx, 3
    rep movsq
    mov rcx, rdx
    and rcx, 7
    rep movsb
    ret

global __fast__strncmp__
__fast__strncmp__:
    test rdx, rdx
    je .done

.byte_loop:
    mov al, [rdi]
    mov r8b, [rsi]
    cmp al, r8b
    jne .return_diff
    test al, al
    je .done
    inc rdi
    inc rsi
    dec rdx
    jnz .byte_loop
    jmp .done

.return_diff:
    movzx eax, al
    movzx ecx, r8b
    sub eax, ecx
    ret

.done:
    xor eax, eax
    ret

global __fast__memchr__
__fast__memchr__:
    test rdx, rdx
    jz .not_found
    
    mov rax, rsi
    mov rcx, rdx
    repne scasb
    jne .not_found
    
    lea rax, [rdi - 1]
    ret

.not_found:
    xor eax, eax
    ret

global __memset32__
__memset32__:
    mov rax, rsi
    mov rcx, rdx
    rep stosd
    ret
