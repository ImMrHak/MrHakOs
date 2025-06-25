; MrHakOS Kernel
; This file contains the main kernel code

[bits 32]       ; We're in 32-bit protected mode

; Define the kernel main function
[global kernel_main]
kernel_main:
    ; Set up video memory pointer (0xb8000 is the start of VGA text mode memory)
    mov edx, 0xb8000
    
    ; Print "WELCOME TO MrHakOS" to the screen
    ; Each character takes 2 bytes - the character itself and its attributes
    ; Attribute byte: Foreground color (lower 4 bits) and background color (upper 3 bits)
    ; 0x0F = White text on black background
    
    ; 'W'
    mov al, 'W'
    mov ah, 0x0F
    mov [edx], ax
    add edx, 2
    
    ; 'E'
    mov al, 'E'
    mov [edx], ax
    add edx, 2
    
    ; 'L'
    mov al, 'L'
    mov [edx], ax
    add edx, 2
    
    ; 'C'
    mov al, 'C'
    mov [edx], ax
    add edx, 2
    
    ; 'O'
    mov al, 'O'
    mov [edx], ax
    add edx, 2
    
    ; 'M'
    mov al, 'M'
    mov [edx], ax
    add edx, 2
    
    ; 'E'
    mov al, 'E'
    mov [edx], ax
    add edx, 2
    
    ; ' '
    mov al, ' '
    mov [edx], ax
    add edx, 2
    
    ; 'T'
    mov al, 'T'
    mov [edx], ax
    add edx, 2
    
    ; 'O'
    mov al, 'O'
    mov [edx], ax
    add edx, 2
    
    ; ' '
    mov al, ' '
    mov [edx], ax
    add edx, 2
    
    ; 'M'
    mov al, 'M'
    mov [edx], ax
    add edx, 2
    
    ; 'r'
    mov al, 'r'
    mov [edx], ax
    add edx, 2
    
    ; 'H'
    mov al, 'H'
    mov [edx], ax
    add edx, 2
    
    ; 'a'
    mov al, 'a'
    mov [edx], ax
    add edx, 2
    
    ; 'k'
    mov al, 'k'
    mov [edx], ax
    add edx, 2
    
    ; 'O'
    mov al, 'O'
    mov [edx], ax
    add edx, 2
    
    ; 'S'
    mov al, 'S'
    mov [edx], ax
    
    ; Infinite loop - hang the system
    jmp $