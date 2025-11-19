global cpu_trampoline

org 0x8000
bits 16

signature: dq 0
pml4_addr: dq 0
data: dq 0

jmp cpu_trampoline

cpu_trampoline:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    
    ; Check signature
    mov eax, [signature]
    mov ebx, 0x00EEDDAB
    cmp ebx, eax
    je .signature_ok
    hlt
    jmp $

.signature_ok:
    ; Enable A20 line
    in al, 0x92
    or al, 2
    out 0x92, al

    lgdt [gdt_descriptor]

    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:core_ap_32

bits 32
core_ap_32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov gs, ax
    mov fs, ax
    mov esp, 0x9F00

    ; Set up PAE paging
    mov eax, [pml4_addr]
    mov cr3, eax

    mov eax, cr4
    or eax, (1 << 5)   ; PAE enable
    mov cr4, eax

    ; Enable long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)   ; LME enable
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, (1 << 31)  ; PG enable
    mov cr0, eax

    cli
    lgdt [gdt64_descriptor]
    jmp 0x08:core_ap_64


extern cpu_trampoline_phase_2
bits 64
core_ap_64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov gs, ax
    mov fs, ax

    mov rbx, [data]
    mov rax, qword [rbx + 8]
    mov rsp, rax
    and rsp, ~0xF

    cld
    mov rax, [pml4_addr]
    mov cr3, rax

    
    cli
    mov rbx, [data + 0]
    mov rax, [rbx]
    call rax 
    jmp $

reloadGDT:     
    push 0x28
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    mov rax, 0x30
    mov ds, rax
    mov es, rax
    mov fs, rax
    mov gs, rax
    mov ss, rax
    ret


; 32-bit GDT
gdt_start:
    dq 0x0000000000000000    ; Null
    dq 0x00CF9A000000FFFF    ; Code32
    dq 0x00CF92000000FFFF    ; Data32
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; 64-bit GDT
gdt64_start:
    dq 0x0000000000000000    ; Null
    dq 0x00209A0000000000    ; Code64
    dq 0x0000920000000000    ; Data64
gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64_start - 1
    dd gdt64_start
