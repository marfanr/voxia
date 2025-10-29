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


    /* Move to the next memory page for .rodata */
    . = . + 0x1000;


    .stivale2hdr : {
        KEEP(*(.stivale2hdr))
    } :rodata

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    .initcalls : ALIGN(8) 
    {
        __start_initcalls = .;
        /* SEQUENCE */
        KEEP(*(.initcalls.simd))
        KEEP(*(.initcalls.gdt))
        KEEP(*(.initcalls.interrupt))
        KEEP(*(.initcalls.phys_base_allocator))
        KEEP(*(.initcalls.paging))
        KEEP(*(.initcalls.vma))
        KEEP(*(.initcalls.block))
        KEEP(*(.initcalls.descriptor))
        KEEP(*(.initcalls.vfs))
        KEEP(*(.initcalls.initrd))
        KEEP(*(.initcalls.graphic))
        KEEP(*(.initcalls.acpi))
        KEEP(*(.initcalls.apic_timer))
        /* end sequence */
        KEEP(*(.initcalls.*))
        __stop_initcalls = .;
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
