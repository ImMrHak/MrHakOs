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

    ; Framebuffer request tag (type 5). GRUB can hand the kernel a linear
    ; graphics framebuffer. On UEFI there is no VGA text mode, so this is the
    ; preferred path for visible output instead of relying on 0xB8000 writes.
    ; Ask for a conservative framebuffer first. GRUB/firmware may return this or
    ; the closest supported real screen mode; the kernel stretches the console
    ; across whatever width/height it actually receives. The tag is optional so
    ; BIOS/VGA systems that cannot provide it still boot instead of failing in GRUB.
    align 8
fb_tag_start:
    dw 5                                  ; tag type: framebuffer
    dw 1                                  ; flags (1 = optional fallback if a firmware cannot provide it)
    dd fb_tag_end - fb_tag_start          ; tag size (20)
    dd 1024                               ; safe preferred width
    dd 768                                ; safe preferred height
    dd 32                                 ; preferred bits-per-pixel
fb_tag_end:

    align 8
    dw 0                                  ; end tag type
    dw 0                                  ; end tag flags
    dd 8                                  ; end tag size
multiboot2_header_end:

section .text
[global _start] ; This is where the linker will place the C++ code

_start:

    ; GRUB Multiboot2 puts magic in EAX and info pointer in EBX.
    ; Save them before touching any registers or the stack.
    mov edi, eax            ; edi = mb2 magic (0x36D76289 if Multiboot2)
    mov esi, ebx            ; esi = mb2 info pointer

    cli
    cld
    mov esp, 0x90000

    ; Pass (magic, info_ptr) to kernel_main via cdecl stack args (right-to-left).
    push esi                ; arg2: mb2 info pointer
    push edi                ; arg1: mb2 magic
    extern kernel_main
    call kernel_main

    ; Halt if kernel_main ever returns.
    halt_loop:
        hlt
        jmp halt_loop