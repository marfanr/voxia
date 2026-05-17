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

int_common:
    pushall

    ; rbx = frame pointer (callee-saved → aman melewati call)
    mov rbx, rsp

    ; ── Bersihkan CR0.TS + CR0.EM supaya fxsave tidak #NM ──
    mov rax, cr0
    and rax, ~((1 << 2) | (1 << 3))
    mov cr0, rax

    ; ── Alokasi + align buffer fxsave ──
    ; Butuh 512 byte, aligned 16
    ; Tambah 16 ekstra untuk jaga-jaga alignment setelah and
    sub rsp, 512 + 16
    and rsp, -16            ; rsp sekarang aligned 16
    fxsave [rsp]            ; simpan FPU state

    ; ── Simpan fpu pointer ke stack SEBELUM adjust alignment ──
    ; Ini kunci keamanan di -O2: tidak andalkan register caller-saved
    push rsp                ; [rsp] = fpu_ptr → ini callee akan lihat sebagai
                            ; local di stack kita sendiri, aman
    ; sekarang rsp % 16 == 8 (karena push 8 byte ke aligned-16 address)
    ; → setelah `call` push ret addr (8 byte) → rsp aligned 16 ✓

    mov rdi, rbx            ; arg1: interrupt_stack_frame_t* (frame)
    mov rsi, [rsp]          ; arg2: fpu_state_t* (fpu_ptr yang baru kita push)
    call vxInterruptHandler

    ; ── Restore FPU dari pointer yang kita simpan di stack ──
    ; rbx masih valid (callee-saved)
    ; [rsp] masih berisi fpu_ptr karena kita tidak pop/add sejak push
    mov rax, [rsp]          ; ambil fpu_ptr dari stack (bukan dari register!)
    fxrstor [rax]

    pop rax                 ; buang fpu_ptr dari stack (balance push di atas)

    ; ── Kembalikan rsp ke posisi setelah pushall ──
    mov rsp, rbx            ; rbx masih valid (callee-saved, tidak berubah)

    popall

    add rsp, 16             ; buang int_no + err_code
    iretq


%macro isr 1
isr_%1:
%if !(%1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17 || %1 == 21 || %1 == 29 || %1 == 30)
    push 0                  ; dummy err_code
%endif
    push %1                 ; int_no
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

; extern syscall
; global syscall_interupt

; syscall_interupt:
;     pushall

;     mov qword [syscall_param +  0], rax
;     mov qword [syscall_param +  8], rdi
;     mov qword [syscall_param + 16], rsi
;     mov qword [syscall_param + 24], rdx
;     mov qword [syscall_param + 32], rcx
;     mov qword [syscall_param + 40], r8
;     mov qword [syscall_param + 48], r9

;     mov rdi, syscall_param
;     call syscall
;     mov qword [syscall_ret], rax

;     popall

;     mov rax, [syscall_ret]
;     iretq


; section .bss
; align 16
; global saved_rax
; saved_rax     resq 1
; syscall_param resq 7        ; rax, rdi, rsi, rdx, rcx, r8, r9
; syscall_ret   resq 1