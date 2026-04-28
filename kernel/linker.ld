OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

PHDRS
{
    null    PT_NULL    FLAGS(0) ;                  
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ;
    rodata  PT_LOAD    FLAGS((1 << 2)) ;           
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ;
    dynamic PT_DYNAMIC FLAGS((1 << 1) | (1 << 2)) ;
}

SECTIONS
{

    . = 0xffffffff80200000;

    .text : {
        *(.text .text.*)
    } :text

    userspace_start = .;
    .userspace : {
        *(.userspace .userspace.*)
    } :text
    userspace_end = .;

    
    .export : {
        KEEP(*(.export))
    } :text


    /* adding a space */
    . = . + 0x1000;


    .stivale2hdr : {
        KEEP(*(.stivale2hdr))
    } :rodata

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    .init_early : ALIGN(8) 
    {
        __init_early_start = .;
        /* SEQUENCE MATTER */
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
        KEEP(*(.init_early.Vnode))
        KEEP(*(.init_early.VfsCache))
        KEEP(*(.init_early.Vfs))
        KEEP(*(.init_early.Dev))
        KEEP(*(.init_early.Mount))
        KEEP(*(.init_early.initrd))
        KEEP(*(.init_early.graphic))
        KEEP(*(.init_early.ioforge))
        KEEP(*(.init_early.KernelReader))

        /* scheduler thread core urutannya jangan terbalik */
        KEEP(*(.init_early.Scheduler))
        KEEP(*(.init_early.Thread))
        KEEP(*(.init_early.Core))
        KEEP(*(.init_early.Workqueue))
        KEEP(*(.init_early.ModuleRegister))
        
        /*
        KEEP(*(.init_early.Virtio))
        KEEP(*(.init_early.ProccessRegister))
        KEEP(*(.init_early.*))
        KEEP(*(.init_early.Task))
        */
        __init_early_end = .;
    } :rodata

    .cpu_trampoline : ALIGN(8)
    {
        __cpu_trampoline_start = .;
        KEEP(*(.cpu_trampoline.*))
        __cpu_trampoline_end = .;
    } :rodata

    .per_core : ALIGN(8)
    {
        __per_core_start = .;
        KEEP(*(.per_core.*))
        __per_core_end = .;
    } :rodata

    . = . + 0x1000;

    .data : {
        *(.data*)
    } :data

    .dynamic : {
        *(.dynamic)
    } :data :dynamic
 
    /* NOTE: .bss needs to be the last thing mapped to :data, otherwise lots of */
    /* unnecessary zeros will be written to the binary. */
    /* If you need, for example, .init_array and .fini_array, those should be placed */
    /* above this. */
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    . = . + 0x1000;
    
    __data_prcpu_start = .;
    .data.percpu : ALIGN(4K) {
      KEEP(*(.data.percpu))     
    } :data
    __data_percpu_end = .;

    /* Discard .note.* and .eh_frame since they may cause issues on some hosts. */
    /* /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    } */
}
