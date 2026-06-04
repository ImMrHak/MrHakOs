// MrHakOS Kernel in C++
#include <stddef.h>
#include <stdint.h>
#include <string.hpp>
#include <vga.hpp>

#include <terminal.hpp>
#include <interrupts.hpp>
#include <filesystem.hpp>
#include <serial.hpp>
#include <network.hpp>
#include <memory.hpp>

static Network kernelNetwork;

// Scan the Multiboot2 info structure for a framebuffer tag and wire it up.
static void mb2_init_fb(Vga& vga, uint32_t mb_info_addr) {
    Serial::writeString("[kernel] scanning MB2 tags for framebuffer\n");
    uint32_t total = *(volatile uint32_t*)mb_info_addr;
    uint8_t* ptr   = (uint8_t*)(mb_info_addr + 8);
    uint8_t* end   = (uint8_t*)mb_info_addr + total;
    while (ptr < end) {
        uint32_t tag_type = *(uint32_t*)ptr;
        uint32_t tag_size = *(uint32_t*)(ptr + 4);
        if (tag_type == 0) break;
        if (tag_type == 8) {
            uint64_t fb_addr  = *(uint64_t*)(ptr + 8);
            uint32_t fb_pitch = *(uint32_t*)(ptr + 16);
            uint32_t fb_w     = *(uint32_t*)(ptr + 20);
            uint32_t fb_h     = *(uint32_t*)(ptr + 24);
            uint8_t  fb_bpp   = *(uint8_t* )(ptr + 28);
            uint8_t  fb_type  = *(uint8_t* )(ptr + 29);
            if (fb_type == 1 && fb_bpp >= 24 && (fb_addr >> 32) == 0) {
                Serial::writeString("[kernel] linear framebuffer found -> FB mode\n");
                vga.init_fb((uint32_t)fb_addr, fb_pitch, fb_w, fb_h, fb_bpp);
            } else if (fb_type == 2) {
                Serial::writeString("[kernel] EGA text framebuffer (VGA text mode kept)\n");
            } else {
                Serial::writeString("[kernel] framebuffer tag unsuitable -> VGA text mode kept\n");
            }
            break;
        }
        ptr += (tag_size + 7) & ~7u;
    }
}

extern "C" void kernel_main(unsigned int mb_magic, unsigned int mb_info_addr) {
    Serial::init();
    Serial::writeString("[kernel] MrHakOS booting\n");
    initKernelMemoryProtection();
    Serial::writeString("[kernel] memory protection initialized\n");

    Vga vga;
    // If booted via GRUB Multiboot2 with a linear framebuffer, switch VGA to it.
    if (mb_magic == 0x36D76289 && mb_info_addr != 0)
        mb2_init_fb(vga, mb_info_addr);
    Serial::writeString("[kernel] vga object ready\n");

    Interrupts interrupts;
    Serial::writeString("[kernel] interrupts object ready\n");
    FileSystem filesystem;
    Serial::writeString("[kernel] filesystem object ready\n");
    Terminal terminal;
    Serial::writeString("[kernel] terminal object ready\n");
    
    interrupts.init();
    Serial::writeString("[kernel] interrupts initialized\n");
    filesystem.init();
    Serial::writeString("[kernel] filesystem initialized\n");
    kernelNetwork.init();
    Serial::writeString("[kernel] network initialized\n");

    terminal.init(&vga, &interrupts, &filesystem, &kernelNetwork);
    
    interrupts.enable();
    Serial::writeString("[kernel] entering terminal\n");
    terminal.run();
}