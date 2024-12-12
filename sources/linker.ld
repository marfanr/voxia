/* Tell the linker that we want an x86_64 ELF64 output file */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

/* We want the symbol _start to be our entry point */
ENTRY(_start)

/* Define the program headers we want so the bootloader gives us the right */
/* MMU permissions */
PHDRS
{
    null    PT_NULL    FLAGS(0) ;                  
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ;
    rodata  PT_LOAD    FLAGS((1 << 2)) ;           
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ;
    dynamic PT_DYNAMIC FLAGS((1 << 1) | (1 << 2)) ;
    /* text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ; /* Execute + Read */
    /* rodata  PT_LOAD    FLAGS((1 << 2)) ;            Read only */
    /* data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ; Write + Read */
}

SECTIONS
{
    /* We wanna be placed in the topmost 2GiB of the address space, for optimisations */
    /* and because that is what the Limine spec mandates. */
    /* Any address in this region will do, but often 0xffffffff80000000 is chosen as */
    /* that is the beginning of the region. */
    . = 0xffffffff80200000;

    .text : {
        *(.text .text.*)
    } :text

    userspace_start = .;
    .userspace : {
        *(.userspace .userspace.*)
    } :text
    userspace_end = .;

    /* Move to the next memory page for .rodata */
    . +=0x1000;

    .stivale2hdr : {
        KEEP(*(.stivale2hdr))
    } :rodata

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    /* Move to the next memory page for .data */
    . +=0x1000;  

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

    /* Discard .note.* and .eh_frame since they may cause issues on some hosts. */
    /* /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    } */
}
