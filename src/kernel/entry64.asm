; MrHakOS 64-bit Kernel Entry Point
[bits 64]

section .signature
[global kernel_signature]
kernel_signature:
    dw 0x8664 ; Magic number to identify valid kernel

section .text
[global _start]
_start:
    ; Basic non-persistent VGA touch; Vga::clear() removes it before prompt.
    mov rdi, 0xB8000
    mov byte [rdi], ' '
    mov byte [rdi+1], 0x0F

    ; Clear direction flag
    cld

    ; Setup a stack in case bootloader didn’t
    mov rsp, 0x9F000

    ; Call kernel_main
    extern kernel_main
    call kernel_main

halt_loop:
    hlt
    jmp halt_loop