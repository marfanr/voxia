global cpu_trampoline

org 0x8000
bits 16

signature: dq 0
pml4_addr: dq 0
data: dq 0

jmp cpu_trampoline

; init bertahap 16 bit -> 32 bit -> 64 bit
cpu_trampoline:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    
    ; cek signature
    ; memastikan tidak ada corrupt
    mov eax, [signature]
    mov ebx, 0x00EEDDAB
    cmp ebx, eax
    je .signature_ok
    ; ngehang
    hlt
    jmp $

.signature_ok:
    ; Enable A20 line
    ; biar bisa pakai ram > 1mb
    in al, 0x92
    or al, 2
    out 0x92, al

    lgdt [gdt_descriptor]

    ; Enable protected mode (32bit)
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

    ; stack sementara
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
    or eax, (1 << 11)     ; NXE - No-Execute Enable
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, (1 << 31)  ; PG enable
    mov cr0, eax

.signature_ok:
    ; Baca LAPIC ID dari CPUID (aman di real mode, tidak butuh GDT)
    mov eax, 1
    cpuid
    shr ebx, 24          ; APIC ID ada di EBX[31:24]
    mov [real_apic_id], ebx   ; simpan dulu sebelum GDT setup

    cli
    lgdt [gdt64_descriptor]
    jmp 0x08:core_ap_64


; extern cpu_trampoline_phase_2
bits 64
core_ap_64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov gs, ax
    mov fs, ax

    cld
    mov rax, [pml4_addr]
    mov cr3, rax 
    cli
    
    mov rbx, [data]
    mov rax, qword [rbx + 8]
    mov rsp, rax
    ; and rsp, ~0xF
    and rsp, -16
    sub rsp, 8
    
    
    mov rbx, [data]
    mov rax, [rbx]
    mov rdi, [real_apic_id]
    call rax 
    jmp $

gdt_start:
    dq 0x0000000000000000    ; Null
    dq 0x00CF9A000000FFFF    ; Code32
    dq 0x00CF92000000FFFF    ; Data32
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; Dummy GDT 64 bit
gdt64_start:
    dq 0x0000000000000000    ; Null
    dq 0x00209A0000000000    ; Code64
    dq 0x0000920000000000    ; Data64
gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64_start - 1
    dq gdt64_start

real_apic_id dd 0