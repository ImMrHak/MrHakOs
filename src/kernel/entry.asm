; MrHakOS Kernel Entry Point
[bits 32]

; Create a special section to hold our signature
section .signature
[global kernel_signature]
kernel_signature:
    dw 0x8664 ; Magic number to identify valid kernel

section .text
[global _start] ; This is where the linker will place the C++ code

_start:

    ; Write 'K' to the VGA buffer to indicate kernel entry started
    mov byte [0xB8000], 'K'
    mov byte [0xB8001], 0x1F    ; White on blue

    ; Make sure we clear interrupts and set up a basic environment
    cli                     ; Clear interrupts
    cld                     ; Clear direction flag for string operations
    
    ; Write 'S' to indicate stack setup is starting
    mov byte [0xB8002], 'S'
    mov byte [0xB8003], 0x1E    ; Yellow on blue
    
    ; Set up a minimal stack in case it wasn't properly initialized
    mov esp, 0x90000       ; Set up a stack
    
    ; Write 'C' to indicate we're about to call kernel_main
    mov byte [0xB8004], 'C'
    mov byte [0xB8005], 0x1A    ; Light green on blue
    
    ; Call the kernel main function
    extern kernel_main
    call kernel_main

    ; Halt if kernel_main ever returns.
    halt_loop:
        hlt
        jmp halt_loop      ; Ensure we keep halting if an NMI occurs