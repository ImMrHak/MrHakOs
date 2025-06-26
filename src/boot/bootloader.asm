; MrHakOS Bootloader - FINAL CORRECTED VERSION

[org 0x7c00]
[bits 16]

main:
    ; --- Step 1: Set up a safe stack and store the boot drive number ---
    mov ax, 0x9000
    mov ss, ax
    mov sp, 0xFFFF
    mov [BOOT_DRIVE], dl

    ; --- Step 2: Print welcome messages ---
    mov si, MSG_LOADING
    call print
    mov si, MSG_DISKLOAD
    call print
    
    ; --- Step 3: Reset the disk system for reliability ---
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error
    
    ; --- Step 4: Load the kernel from disk (the main operation) ---
    mov ah, 0x02
    ; *** THE FIX: Load 32 sectors (16KB) to fit the entire kernel. ***
    mov al, 32
    mov ch, 0
    mov dh, 0
    mov cl, 2
    mov dl, [BOOT_DRIVE]
    ; Set destination ES:BX to 0x1000:0000 (physical 0x10000)
    mov bx, 0x1000
    mov es, bx
    mov bx, 0
    
    int 0x13
    jnc .read_success
    ; If the read fails, hang with an error message.
    jmp disk_error
    
.read_success:
    ; --- Step 5: Verify that the loaded data is a valid kernel ---
    mov si, MSG_VERIFY
    call print
    
    mov ax, [es:0] ; Check the signature at the start of the loaded data
    cmp ax, 0x8664
    jne kernel_error
    
.sig_ok:
    ; --- Step 6: All checks passed. Prepare for Protected Mode. ---
    mov si, MSG_SUCCESS
    call print
    
    cli                     ; Disable interrupts to prevent triple faults
    lgdt [gdt_descriptor]   ; Load the GDT
    lidt [idt_descriptor]   ; CRITICAL: Load an empty IDT to stabilize the system
    
    ; --- Step 7: Enable Protected Mode and jump to the kernel ---
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    
    jmp CODE_SEG:enter_pm

[bits 32]
enter_pm:
    ; We are now in 32-bit Protected Mode.
    ; Write a 'P' to the screen to confirm entry.
    mov dword [0xB8000], 0x1F50

    ; Set up all segment registers for a flat memory model
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up the final stack and jump to the kernel entry point
    mov esp, 0x9FFFF
    jmp 0x10000

; --- Procedures & Data ---
print:
    mov ah, 0x0e
.loop:
    lodsb; or al, al; jz .done; int 0x10; jmp .loop
.done:
    ret

disk_error:
    mov si, MSG_ERROR
    call print
    jmp $

kernel_error:
    mov si, MSG_KERNEL_ERROR
    call print
    jmp $

BOOT_DRIVE db 0

; GDT - Using simplified but effective definitions
gdt_start:
    dd 0, 0                         ; Null Descriptor
gdt_code:
    dw 0xFFFF, 0, 0x9A00, 0x00CF    ; 4GB Code Segment
gdt_data:
    dw 0xFFFF, 0, 0x9200, 0x00CF    ; 4GB Data Segment
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; An empty but valid IDT is crucial to prevent reboots
idt_descriptor:
    dw 0 ; Size = 0
    dd 0 ; Address = 0

; Segment Selectors
CODE_SEG equ gdt_code - gdt_start   ; Should be 0x08
DATA_SEG equ gdt_data - gdt_start   ; Should be 0x10

; Messages
MSG_LOADING db 'MRHAKOS BOOTLOADER', 0x0D, 0x0A, 0
MSG_DISKLOAD db 'Loading kernel...', 0x0D, 0x0A, 0
MSG_SUCCESS  db ' OK', 0x0D, 0x0A, 0
MSG_VERIFY   db 'Verifying kernel...', 0
MSG_ERROR   db 'DISK ERROR!', 0
MSG_KERNEL_ERROR db 'INVALID KERNEL!', 0

; Boot signature
times 510 - ($ - $$) db 0
dw 0xAA55