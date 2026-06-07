; MrHakOS 64-bit Kernel Entry Point
[bits 64]

section .signature
[global kernel_signature]
kernel_signature:
    dw 0x8664 ; Magic number to identify valid kernel

section .text align=8
[global _start]
_start:
    ; The 32-bit bootstrap (or BIOS loader) passes the Multiboot2 magic in RDI
    ; and the info pointer in RSI per the SysV ABI. The VGA touch below only
    ; clobbers RAX, so RDI/RSI survive untouched all the way to kernel_main.
    ; Do NOT push/pop them around a stack-pointer reset: resetting RSP between
    ; the push and pop made the pops read garbage and handed kernel_main a bogus
    ; magic/info, which disabled framebuffer setup and gave a black screen under
    ; UEFI (no VGA text fallback there).
    cld

    ; Mask every legacy PIC interrupt and keep IF clear until the kernel installs
    ; its own IDT in interrupts.init(). GRUB/OVMF on UEFI hand the kernel off with
    ; the 8259 PICs still unmasked and the PIT running, so an IRQ0 (timer) would
    ; fire into a not-yet-installed handler, page-fault, and triple-fault the box
    ; (black screen on real UEFI hardware). The legacy BIOS path happened to leave
    ; the PICs masked, which is why this only bit under UEFI.
    cli
    mov al, 0xFF
    out 0x21, al                 ; mask all IRQs on the master PIC
    out 0xA1, al                 ; mask all IRQs on the slave PIC

    ; Setup a stack in case the loader did not.
    mov rsp, 0x9F000

    ; Call kernel_main(mb_magic, mb_info).
    ; 64-bit SysV ABI: first arg in RDI, second in RSI (already set).
    extern kernel_main
    call kernel_main

halt_loop:
    hlt
    jmp halt_loop