OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

PHDRS
{
    null    PT_NULL    FLAGS(0);
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)); /* RX */
    rodata  PT_LOAD    FLAGS((1 << 2));            /* R  */
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)); /* RW */
    dynamic PT_DYNAMIC FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    . = 0xffffffff80200000;

    /* ========================================================= */
    /* TEXT                                                      */
    /* ========================================================= */

    .text : ALIGN(4K)
    {
        *(.text .text.*)
    } :text

    userspace_start = .;

    .userspace : ALIGN(4K)
    {
        *(.userspace .userspace.*)
    } :text

    userspace_end = .;

    .export : ALIGN(16)
    {
        KEEP(*(.export))
    } :text

    . = ALIGN(4K);

    /* read only */
    .stivale2hdr : ALIGN(16)
    {
        KEEP(*(.stivale2hdr))
    } :rodata

    .rodata : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    } :rodata

    .init_early : ALIGN(8)
    {
        __init_early_start = .;

        KEEP(*(.init_early.SIMD))
        KEEP(*(.init_early.Gdt))
        KEEP(*(.init_early.Interrupt))
        KEEP(*(.init_early.phys_base_allocator))
        KEEP(*(.init_early.paging))
        KEEP(*(.init_early.vma))
        KEEP(*(.init_early.Proccess))
        KEEP(*(.init_early.descriptor))
        KEEP(*(.init_early.acpi))
        KEEP(*(.init_early.Timer))
        KEEP(*(.init_early.Rand))
        KEEP(*(.init_early.VfsCache))
        KEEP(*(.init_early.Vnode))
        KEEP(*(.init_early.Vfs))
        KEEP(*(.init_early.initrd))
        KEEP(*(.init_early.graphic))     
        KEEP(*(.init_early.ioforge))
        KEEP(*(.init_early.Virtio))
        KEEP(*(.init_early.KernelReader))
        KEEP(*(.init_early.Scheduler))
        KEEP(*(.init_early.Thread))
        KEEP(*(.init_early.Process))
        KEEP(*(.init_early.Core))
        KEEP(*(.init_early.Workqueue))
        KEEP(*(.init_early.ModuleRegister))

        /*
        KEEP(*(.init_early.Syscall))


        KEEP(*(.init_early.Netbuff))
        KEEP(*(.init_early.Netdev))
        KEEP(*(.init_early.Socket))

        

        KEEP(*(.init_early.ProccessRegister))
        


        */

        /* KEEP(*(.init_early.*)) */

        __init_early_end = .;
    } :rodata

    .cpu_trampoline : ALIGN(4K)
    {
        __cpu_trampoline_start = .;
        KEEP(*(.cpu_trampoline.*))
        __cpu_trampoline_end = .;
    } :rodata

    .per_core : ALIGN(4K)
    {
        __per_core_start = .;
        KEEP(*(.per_core.*))
        __per_core_end = .;
    } :rodata

    . = ALIGN(4K);

    /* ========================================================= */
    /* DATA                                                      */
    /* ========================================================= */

    .data : ALIGN(4K)
    {
        *(.data .data.*)
    } :data

    .dynamic : ALIGN(16)
    {
        *(.dynamic)
    } :data :dynamic

    .data.percpu : ALIGN(4K)
    {
        __data_percpu_start = .;
        KEEP(*(.data.percpu))
        __data_percpu_end = .;
    } :data

    /* ========================================================= */
    /* BSS (MUST BE LAST IN :data SEGMENT)                       */
    /* ========================================================= */

    .bss : ALIGN(4K)
    {
        __bss_start = .;

        *(COMMON)
        *(.bss .bss.*)

        __bss_end = .;
    } :data

    . = ALIGN(4K);

    /* ========================================================= */
    /* DISCARD                                                   */
    /* ========================================================= */

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.note .note.*)
    }
}