/* MrHakOS Linker Script */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

SECTIONS {
    /* The kernel will be loaded at 64KB (0x10000) by the bootloader */
    . = 0x10000;
    
    /* Put our signature section first */
    .signature : {
        *(.signature)
    }
    
    /* Text section follows */
    .text : ALIGN(4) {
        *(.text*)              /* All text sections */
    }
    
    /* Other sections follow */
    .rodata : ALIGN(4) {
        *(.rodata*)
    }
    
    .data : ALIGN(4) {
        *(.data*)
    }
    
    .bss : ALIGN(4) {
        *(.bss*)
    }
}