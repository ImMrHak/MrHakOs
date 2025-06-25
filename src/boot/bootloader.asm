; MrHakOS Bootloader
; This is a simple bootloader for MrHakOS

[org 0x7c00]    ; Origin, tell the assembler where the code will be loaded
[bits 16]       ; We're working in 16-bit real mode

; Setup the stack
mov bp, 0x9000  ; Set the base of the stack a bit away from origin
mov sp, bp      ; Stack grows downwards from bp

; Clear the screen
mov ah, 0x00    ; Set video mode function
mov al, 0x03    ; 80x25 text mode
int 0x10        ; BIOS video interrupt

; Print welcome message
mov si, welcome_msg
call print_string

; Infinite loop - hang the system
jmp $

; Function to print a null-terminated string
; Input: SI = pointer to string
print_string:
    pusha           ; Save all registers
    mov ah, 0x0e    ; BIOS teletype function

print_char:
    lodsb           ; Load byte at SI into AL and increment SI
    or al, al       ; Check if AL is 0 (end of string)
    jz print_done   ; If AL is 0, we're done
    int 0x10        ; Otherwise, print the character
    jmp print_char  ; And continue with the next character

print_done:
    popa            ; Restore all registers
    ret             ; Return to caller

; Data
welcome_msg db 'WELCOME TO MrHakOS', 0

; Padding and magic number
times 510-($-$$) db 0   ; Fill the rest of the sector with zeros
dw 0xaa55               ; Boot signature at the end of the bootloader