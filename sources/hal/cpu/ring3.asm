global jump_usermode
extern test_user_function
jump_usermode:
    mov ax, (0x40) | 3        ; Ring 3 data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax                ; Setel segmen data untuk Ring 3

    ; Set up stack frame untuk IRET
    mov rax, user_stack       ; Alamat stack user (pre-allocated)
    push (0x40) | 3           ; Ring 3 data segment selector
    push rax                  ; ESP (stack pointer untuk user mode)
    pushf                     ; EFLAGS (ambil dari state saat ini)
    push (0x38) | 3           ; Ring 3 code segment selector
    push rdi                  ; Alamat program user (RIP dari RDI)

    iretq                     ; Switch ke user mode

section .bss
    align 16
    user_stack resq 512       ; 4KB stack untuk user mode

	; add rsp, 8 ; skip over the pushed rdi

global umode
umode:
	jmp rdi
	jmp $
	