/* MrHakOS 64-bit Linker Script */
ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS {
    /* Load at 64KB (0x10000) to match bootloader */
    . = 0x10000;

    .signature : {
        *(.signature)
    }

    __text_start = .;
    .text : ALIGN(8) {
        *(.text*)
    }
    __text_end = .;

    . = ALIGN(4096);
    __rodata_start = .;
    .rodata : ALIGN(8) {
        *(.rodata*)
    }
    __rodata_end = .;

    /* Keep the 64-bit flat binary inside the 128-sector BIOS loader window.
       The early 64-bit path does not install page permissions yet, so page-aligning
       .data here only pads the boot image without improving protection. */
    . = ALIGN(8);
    __data_start = .;
    .data : ALIGN(8) {
        *(.init_array*)
        *(.data*)
    }
    __data_end = .;

    . = ALIGN(4096);
    __bss_start = .;
    .bss : ALIGN(8) {
        *(COMMON)
        *(.bss*)
    }
    __bss_end = .;

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
    }
}