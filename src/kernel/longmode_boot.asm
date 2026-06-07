; MrHakOS Multiboot2 -> x86_64 long-mode bootstrap
;
; GRUB starts Multiboot2 kernels in 32-bit protected mode. This loader brings up
; long mode with a broad identity map and jumps to the 64-bit kernel.
;
; Design note (why the 64-bit kernel is embedded instead of a GRUB module):
; The 64-bit kernel is linked to RUN at 0x10000. Earlier this loader asked GRUB
; to load kernel64.bin as a module and copied it from wherever GRUB put it down
; to 0x10000. That works under legacy BIOS (GRUB loads the module high, e.g.
; ~0x11C000) but is broken under UEFI: GRUB packs the module into low memory
; (observed at 0x3000), which OVERLAPS the 0x10000 destination, so the forward
; copy clobbers its own source. GRUB also reported a bogus module end there.
; To be firmware-independent we now embed kernel64.bin directly in this loader
; (incbin) so its source always lives high alongside this code, and we relocate
; GRUB's Multiboot info to a safe high address before overwriting 0x10000.

[bits 32]

MULTIBOOT2_MAGIC equ 0xE85250D6
KERNEL64_BASE    equ 0x10000
KERNEL64_ENTRY   equ 0x10010
; GRUB's Multiboot info can sit in low memory (near 0x10000 under UEFI). Copy it
; here, well above this loader, its embedded kernel, and the page tables, so the
; kernel can still read the framebuffer/cmdline tags after we stomp 0x10000.
MBI_SCRATCH      equ 0x00300000

section .multiboot align=8
multiboot2_header_start:
    dd MULTIBOOT2_MAGIC
    dd 0                                  ; protected-mode i386 architecture
    dd multiboot2_header_end - multiboot2_header_start
    dd -(MULTIBOOT2_MAGIC + 0 + (multiboot2_header_end - multiboot2_header_start))

    ; Optional framebuffer request. The size field must span the WHOLE tag
    ; (type+flags+size words plus the width/height/depth body == 20 bytes);
    ; using only the 12-byte body makes GRUB misread the depth field as the next
    ; tag and abort with "unsupported tag: 0x20".
    align 8
fb_tag_start:
    dw 5
    dw 1                                  ; optional
    dd fb_tag_end - fb_tag_start
    dd 800
    dd 600
    dd 32
fb_tag_end:

    align 8
    dw 0
    dw 0
    dd 8
multiboot2_header_end:

section .text align=16
global _start
_start:
    cli
    cld
    mov esp, stack_top
    mov [mb_magic_saved], eax

    ; Relocate GRUB's Multiboot info (ebx) to MBI_SCRATCH. The first dword of the
    ; info block is its total size. Done first, before we overwrite 0x10000.
    test ebx, ebx
    jz .skip_mbi
    mov esi, ebx
    mov ecx, [ebx]                        ; total MBI size in bytes
    mov edi, MBI_SCRATCH
    rep movsb
.skip_mbi:

    ; Copy the embedded 64-bit kernel down to its link/run address 0x10000.
    ; Source (this loader's .rodata, ~0x101000+) never overlaps the destination.
    mov esi, kernel64_blob
    mov edi, KERNEL64_BASE
    mov ecx, kernel64_blob_end - kernel64_blob
    rep movsb

    call setup_page_tables

    lgdt [gdt64_ptr]

    ; Enable PAE.
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Load PML4.
    mov eax, pml4
    mov cr3, eax

    ; Enable long mode in EFER.
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable paging + protected mode.
    mov eax, cr0
    or eax, (1 << 31) | 1
    mov cr0, eax

    jmp 0x08:long_mode_entry

setup_page_tables:
    ; Clear PML4, PDPT and 16 page directories.
    mov edi, pml4
    xor eax, eax
    mov ecx, (4096 * 18) / 4
    rep stosd

    mov dword [pml4 + 0], pdpt + 0x003
    mov dword [pml4 + 4], 0

    ; Map first 16 GiB using 2 MiB pages: enough for most UEFI GOP framebuffers.
    xor ebx, ebx                            ; PDPT index / GiB index
.next_pdpt:
    cmp ebx, 16
    jae .done_tables
    mov eax, ebx
    shl eax, 12
    add eax, pd0
    or eax, 0x003
    mov [pdpt + ebx * 8], eax
    mov dword [pdpt + ebx * 8 + 4], 0

    ; Fill one page directory with 512 2MiB mappings.
    mov edi, ebx
    shl edi, 12
    add edi, pd0
    xor ecx, ecx
.next_pde:
    mov eax, ebx
    shl eax, 30                             ; low32(base) = GiB index * 1GiB
    mov edx, ecx
    shl edx, 21                             ; + PDE index * 2MiB
    add eax, edx
    or eax, 0x083                           ; present | rw | huge 2MiB
    mov [edi + ecx * 8], eax
    mov eax, ebx
    shr eax, 2                              ; high32(base) for every 4 GiB
    mov [edi + ecx * 8 + 4], eax
    inc ecx
    cmp ecx, 512
    jne .next_pde

    inc ebx
    jmp .next_pdpt
.done_tables:
    ret

[bits 64]
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, long_stack_top
    ; kernel_main(mb_magic, mb_info): magic in EDI, relocated info in ESI.
    mov edi, [mb_magic_saved]
    mov esi, MBI_SCRATCH
    mov rax, KERNEL64_ENTRY
    jmp rax

section .rodata align=16
gdt64:
    dq 0
    dq 0x00af9a000000ffff                   ; 64-bit code
    dq 0x00af92000000ffff                   ; data
gdt64_end:
gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dd gdt64

; The 64-bit kernel image, embedded so it always lives high with this loader
; regardless of where (or whether) GRUB would have placed it as a module.
align 16
global kernel64_blob
kernel64_blob:
    incbin "bin/kernel64.bin"
kernel64_blob_end:

section .bss align=4096
pml4:  resb 4096
pdpt:  resb 4096
pd0:   resb 4096 * 16

align 16
mb_magic_saved: resd 1

align 16
stack_bottom: resb 16384
stack_top:
long_stack_bottom: resb 16384
long_stack_top:
