; MrHakOS Kernel Entry Point
; This file contains the entry point for the kernel

[bits 32]       ; We're in 32-bit protected mode

; Kernel entry point
[global _start]
_start:
    ; Set up kernel environment
    call kernel_main  ; Call the main kernel function
    jmp $             ; Infinite loop if we ever return

; Include the kernel
[extern kernel_main]  ; Declare that kernel_main is defined elsewhere