/* MrHakOS 32-bit Multiboot2 long-mode bootstrap linker */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

SECTIONS {
    . = 0x100000;

    .multiboot : ALIGN(8) {
        *(.multiboot*)
    }

    .text : ALIGN(16) {
        *(.text*)
    }

    .rodata : ALIGN(8) {
        *(.rodata*)
    }

    .data : ALIGN(16) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
    }
}
