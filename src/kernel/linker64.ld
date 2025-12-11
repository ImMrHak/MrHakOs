/* MrHakOS 64-bit Linker Script */
ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS {
    /* Load at 64KB (0x10000) to match bootloader */
    . = 0x10000;

    .signature : {
        *(.signature)
    }

    .text : ALIGN(8) {
        *(.text*)
    }

    .rodata : ALIGN(8) {
        *(.rodata*)
    }

    .data : ALIGN(8) {
        *(.data*)
    }

    .bss : ALIGN(8) {
        *(.bss*)
    }
}