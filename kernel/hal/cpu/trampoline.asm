bits 16
global core_trampoline
section .text

core_trampoline:
    ; Save the registers
    pusha

    ; Load the segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Call the C function
    

    ; Restore the registers
    popa

    ; Return to the caller
    ret