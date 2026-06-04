; MrHakOS Kernel Entry Point
[bits 32]

; Create a special section to hold our signature.
; The custom BIOS bootloader expects this at the start of the flat binary.
section .signature
[global kernel_signature]
kernel_signature:
    dw 0x8664 ; Magic number to identify valid kernel

; Multiboot2 header so GRUB can load the 32-bit kernel directly from the
; existing Kali GRUB menu without chainloading the raw floppy image.
; GRUB requires this header within the first 32 KiB and aligned to 8 bytes.
section .multiboot align=8
multiboot2_header_start:
    dd 0xE85250D6                         ; magic
    dd 0                                  ; protected-mode i386 architecture
    dd multiboot2_header_end - multiboot2_header_start
    dd -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header_start))

    ; Framebuffer request tag - ask GRUB to keep/provide text mode (type=0)
    ; This silences the "no console will be available" warning and tells GRUB
    ; we want VGA text mode (width=80, height=25, depth=0 means text mode)
    align 8
    dw 5                                  ; tag type: framebuffer request
    dw 0                                  ; flags: optional
    dd 20                                 ; tag size
    dd 80                                 ; preferred width (columns)
    dd 25                                 ; preferred height (rows)
    dd 0                                  ; preferred depth (0 = text mode)

    align 8
    dw 0                                  ; end tag type
    dw 0                                  ; end tag flags
    dd 8                                  ; end tag size
multiboot2_header_end:

section .text
[global _start] ; This is where the linker will place the C++ code

_start:

    ; Make sure we clear interrupts and set up a basic environment
    cli                     ; Clear interrupts
    cld                     ; Clear direction flag for string operations
    
    ; Set up a minimal stack in case it wasn't properly initialized
    mov esp, 0x90000       ; Set up a stack
    
    ; Call the kernel main function
    extern kernel_main
    call kernel_main

    ; Halt if kernel_main ever returns.
    halt_loop:
        hlt
        jmp halt_loop      ; Ensure we keep halting if an NMI occurs