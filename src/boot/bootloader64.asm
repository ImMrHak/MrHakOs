; MrHakOS 64-bit Bootloader (BIOS, Long Mode)

[org 0x7c00]
[bits 16]

main:
    ; Basic stack and boot drive
    mov ax, 0x9000
    mov ss, ax
    mov sp, 0xFFFF
    mov [BOOT_DRIVE], dl

    ; Visual debug: write 'B' to VGA to mark boot sector start
    ; Use segment addressing to avoid 16-bit immediate overflow
    mov ax, 0xB800
    mov es, ax
    mov word [es:0], 0x1F42

    ; (Removed BIOS teletype printing to save space)

    ; (Removed pause to keep boot sector under 512 bytes)

    ; Reset disk
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error

    ; Load kernel (start at sector 2) to 0x10000
    mov ah, 0x02
    mov al, 64                ; read 64 sectors (~32KB) to fit C++ kernel
    mov ch, 0
    mov dh, 0
    mov cl, 2
    mov dl, [BOOT_DRIVE]
    ; destination ES:BX => 0x1000:0000 = 0x10000
    mov bx, 0x1000
    mov es, bx
    xor bx, bx
    int 0x13
    jnc .read_success
    jmp disk_error

.read_success:

    ; Verify signature
    mov ax, [es:0]
    cmp ax, 0x8664
    jne kernel_error

    ; (Removed OK print to save space)

    ; (Removed pause to keep boot sector under 512 bytes)

    ; Enable A20 (simple method; QEMU often has it enabled)
    call enable_a20

    ; Enter Protected Mode first
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1               ; CR0.PE
    mov cr0, eax
    jmp CODE32_SEL:enter_pm

[bits 32]
enter_pm:
    ; Debug: write 'P' to VGA to indicate Protected Mode entry
    mov ebx, 0xB8000
    mov dword [ebx], 0x1F50

    ; (Removed pause to keep boot sector under 512 bytes)

    ; Load data segments
    mov ax, DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up a known-good stack top
    mov esp, 0x9FFFF

    ; Build minimal page tables for long mode (no full clears)
    ; Layout: identity map first 2MB using 2MB page
    ; PML4 @ 0x90000, PDPT @ 0x91000, PD @ 0x92000

    ; PML4[0] = PDPT | P | RW
    mov dword [0x90000], 0x91000 | 0x3
    mov dword [0x90000+4], 0x0

    ; PDPT[0] = PD | P | RW
    mov dword [0x91000], 0x92000 | 0x3
    mov dword [0x91000+4], 0x0

    ; PD[0] = 2MB page @ 0x0 | P | RW | PS
    mov dword [0x92000], 0x00000000 | 0x3 | (1 << 7)
    mov dword [0x92000+4], 0x0

    ; Load CR3 with PML4
    mov eax, 0x90000
    mov cr3, eax

    ; Enable PAE
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; Enable Long Mode via EFER.LME
    mov ecx, 0xC0000080       ; IA32_EFER
    rdmsr
    or eax, (1 << 8)
    wrmsr

    ; Enable paging (CR0.PG) while keeping PE set
    mov eax, cr0
    or eax, (1 << 31) | 0x1
    mov cr0, eax

    ; Load 64-bit GDT and jump to 64-bit
    lgdt [gdt64_descriptor]
    jmp CODE64_SEL:enter_lm

[bits 64]
enter_lm:
    ; Long mode entry
    ; Set a basic stack away from page tables (avoid 0x90000..0x92000)
    mov rsp, 0x9F000

    ; In long mode, segment bases are ignored but selectors must be valid.
    ; Load a proper data selector for SS/DS/ES/FS/GS to avoid #GP and triple fault.
    mov ax, DATA64_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Jump to kernel entry
    mov rax, 0x10000 + 8
    jmp rax

; --- Minimal helpers removed to fit within 512 bytes ---

enable_a20:
    ; Fast A20 gate enable via port 0x92
    in al, 0x92
    or al, 0x02            ; set bit1 (A20 enable)
    out 0x92, al
    ret

disk_error:
    ; Show 'E' at (0,2) and halt
    mov ax, 0xB800
    mov es, ax
    mov word [es:2], 0x1F45
    jmp $

kernel_error:
    ; Show '!' at (0,4) and halt
    mov ax, 0xB800
    mov es, ax
    mov word [es:4], 0x1F21
    jmp $

BOOT_DRIVE db 0

; 32-bit GDT
gdt32_start:
    dq 0x0000000000000000      ; null
gdt32_code:
    dq 0x00CF9A000000FFFF      ; 32-bit code
gdt32_data:
    dq 0x00CF92000000FFFF      ; 32-bit data
gdt32_end:

gdt_descriptor:
    dw gdt32_end - gdt32_start - 1
    dd gdt32_start

CODE32_SEL equ (gdt32_code - gdt32_start)
DATA32_SEL equ (gdt32_data - gdt32_start)

; 64-bit GDT (L-bit set on code)
gdt64_start:
    dq 0x0000000000000000      ; null
gdt64_code:
    dq 0x00209A0000000000      ; 64-bit code segment
gdt64_data:
    dq 0x0000920000000000      ; 64-bit data segment
gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64_start - 1
    dq gdt64_start

CODE64_SEL equ (gdt64_code - gdt64_start)
DATA64_SEL equ (gdt64_data - gdt64_start)

; (Messages removed to reduce boot sector size)

; Boot signature
times 510 - ($ - $$) db 0
dw 0xAA55