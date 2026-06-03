/* MrHakOS 32-bit Linker Script */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

PHDRS {
    sig     PT_LOAD FLAGS(4); /* R */
    text    PT_LOAD FLAGS(5); /* R + X */
    rodata  PT_LOAD FLAGS(4); /* R */
    data    PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS {
    /* The kernel will be loaded at 64KB (0x10000) by the bootloader. */
    . = 0x10000;

    /* Put the bootloader-validated signature first in the flat binary. */
    .signature : {
        *(.signature)
    } :sig

    . = ALIGN(8);
    .multiboot : {
        *(.multiboot*)
    } :text

    . = ALIGN(16);
    .text : {
        *(.text*)
    } :text

    . = ALIGN(16);
    .rodata : {
        *(.rodata*)
        *(.eh_frame*)
    } :rodata

    . = ALIGN(16);
    .data : {
        *(.data*)
        *(.got*)
        *(.got.plt*)
    } :data

    . = ALIGN(16);
    .bss : {
        *(COMMON)
        *(.bss*)
    } :data

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
    }
}
