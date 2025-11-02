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
    vzeroupper
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
    vzeroupper
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
    vzeroupper
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
    vzeroupper
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
    vzeroupper
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
    vzeroupper
    ret