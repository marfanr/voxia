global __fast_memset__
__fast_memset__:
    ; check size is not 0
    test rdx, rdx
    jz .done

    movd xmm0, esi
    vpbroadcastb ymm0, xmm0

    mov rcx, rdx
    shr rcx, 5
    jz .tail
align 32
.loop:
    vmovdqu [rdi], ymm0
    add     rdi, 32
    dec     rcx
    jnz     .loop
.tail:
    and     rdx, 31
    jz      .done
.tail_loop:
    mov     [rdi], sil
    inc     rdi
    dec     rdx
    jnz     .tail_loop
.done:
    ret

global __fast_memset_aligned__
__fast_memset_aligned__:
    ; check size is not 0
    test rdx, rdx
    jz .done

    movq xmm0, rsi
    vpbroadcastb ymm0, xmm0

    mov rcx, rdx
    shr rcx, 5
    jz .tail

align 32
.loop:
    prefetchnta [rdi+256]
    vmovntdq [rdi], ymm0
    add     rdi, 32
    dec     rcx
    jnz     .loop

.tail:
    and     rdx, 31
    jz      .done

.tail_loop:
    mov     [rdi], sil
    inc     rdi
    dec     rdx
    jnz     .tail_loop
.done:
    ret

global __fast__memcpy__
__fast__memcpy__:
    test rdx, rdx
    jz .done

    mov rcx, rdx
    shr rcx, 5
    jz .tail
align 32
.loop:
    prefetchnta [rsi+256]
    vmovdqu ymm0, [rsi]
    vmovdqu [rdi], ymm0
    add rsi, 32
    add rdi, 32
    dec rcx
    jnz     .loop
.tail:
    and     rdx, 31
    jz      .done
.tail_loop:
    mov     al, [rsi]
    mov     [rdi], al
    inc     rsi
    inc     rdi
    dec     rdx
    jnz .tail_loop
.done:
    ret

global __fast__memcpy_aligned__
__fast__memcpy_aligned__:
    test rdx, rdx
    jz .done

    mov rcx, rdx
    shr rcx, 5
    jz .tail
align 32
.loop:
    prefetchnta [rsi+256]
    vmovdqa ymm0, [rsi]
    vmovdqa [rdi], ymm0
    add rsi, 32
    add rdi, 32
    dec rcx
    jnz     .loop
.tail:
    and     rdx, 31
    jz      .done
.tail_loop:
    mov     al, [rsi]
    mov     [rdi], al
    inc     rsi
    inc     rdi
    dec     rdx
    jnz .tail_loop
.done:
    ret


global __fast__strncmp__
section .text
__fast__strncmp__:
    test rdx, rdx
    je .done                 ; jika n == 0, return 0

.loop32:
    cmp rdx, 32
    jb .tail                 ; sisa <32, pakai loop byte biasa

    vmovdqu ymm0, [rdi]      ; load 32 byte s1
    vmovdqu ymm1, [rsi]      ; load 32 byte s2

    vpcmpeqb ymm2, ymm0, ymm1 ; compare byte per byte
    vpmovmskb eax, ymm2        ; mask 32-bit, 1 jika sama

    cmp eax, 0xFFFFFFFF
    jne .mismatch             ; ada beda

    ; cek null terminator
    vpxor ymm3, ymm3, ymm3    ; register zero
    vpcmpeqb ymm4, ymm0, ymm3
    vpmovmskb ecx, ymm4
    test ecx, ecx
    jne .null_found

    add rdi, 32
    add rsi, 32
    sub rdx, 32
    jmp .loop32

.tail:
    test rdx, rdx
    je .done
.byte_loop:
    mov al, [rdi]
    mov bl, [rsi]
    cmp al, bl
    jne .return_diff
    test al, al
    je .done
    inc rdi
    inc rsi
    dec rdx
    jnz .byte_loop
    jmp .done

.mismatch:
    ; cari byte pertama berbeda dalam block 32-byte
    mov rcx, 0
.byte_scan:
    cmp rcx, 32
    je .done
    mov al, [rdi + rcx]
    mov bl, [rsi + rcx]
    cmp al, bl
    jne .return_diff
    test al, al
    je .done
    inc rcx
    jmp .byte_scan

.null_found:
    ; scan untuk null terminator
    mov rcx, 0
.null_scan:
    cmp rcx, 32
    je .done
    mov al, [rdi + rcx]
    mov bl, [rsi + rcx]
    cmp al, bl
    jne .return_diff
    test al, al
    je .done
    inc rcx
    jmp .null_scan

.return_diff:
    movzx eax, al
    movzx ecx, bl
    sub eax, ecx
    ret

.done:
    xor eax, eax
    ret
