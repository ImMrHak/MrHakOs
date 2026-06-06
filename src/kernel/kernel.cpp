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

static void bootPut(Vga& vga, int x, int y, const char* text, int color) {
    vga.set_color(color);
    vga.set_xy(x, y);
    for (int i = 0; text[i]; i++) vga.putChar(text[i]);
}

static void bootPutCentered(Vga& vga, int y, const char* text, int color) {
    int len = 0;
    while (text[len]) len++;
    bootPut(vga, (80 - len) / 2, y, text, color);
}

static void showBootAnimation(Vga& vga) {
    vga.set_cursor_enabled(false);
    vga.set_color(0x00);
    vga.clear();

    // --- Phase 1: Matrix-rain glitch flash (300ms) ---
    static const char* rain[] = {
        "4f2e89a4c3f17b3d92e85c0ab644ff1e73c92f88d56b3794e20bf517ace3b96d02f485c19e7a3",
        "921fb46e30d753aa2c87f948156dc28b3f76e4095ab82d9143fc1867de34a509c7b012ef658bd",
        "0d5e9b2a76c4318fe5134c976b20d358a7f24e85193c70bd620f459c28e1b74a03961df5820c6",
        "b37e015c90d4276fc1488d3572ab1e63f70c599e843a6db02578cd42075bfe314a8069dc25e91",
        "68ad144f93287ce0359a51065fb4297ed2176ca1368b4df02378c50a5f94b831c0e46279d1a3f",
        "3c819fe50d742ab96f1384d52c7b0e69af2d85173be4c06a91f28d47530c1e8b24967a5fd0e82",
        "a52d7f901b4e836c24f719d83c56b20e74a917f3d2568bca041e7290fd63b1485ca2e39071d46",
        "e80c3f7b14a9562d047e8a1cb39df65402e7b8c5319a047d2f8e6b9350c1742a08f5bcd639175",
        "17b43c9e506a2df81e7450bc9362fd8a014c5e97b2380fd64a15c7e293b046d8f5a126e9d07c3",
        "c6fa3081759b2e4d037cf1852a69de40b378e9c61f4507d2b8390afe1d65042c793b16e8524ad",
        "49d2a0871f5e36bc4097e2d8530c6f14a7b9280e5137dc64fe092b5840e73a1c26fd95b80e3f1",
        "8b0e7f3c4a19256de08147f3bc5a9e20d6148f5c3e07b92a481c97d5640f3be29a07d1c8e5f04",
        "2f5c8a0163e74bfd9215c4e089a7f62d4b139ce750f8e214ba63907d5c2fe48190a6d7b5e3c87",
        "d1937b54f8026cea4518db2f0973c5e840691b7fa3e5d2c08476ef91b302c457a9fe1d08b6a23",
        "605e4bc27390f1da8e72b1053c94fa67d2108e3f59bc4a7602d1f8953e67bc104da2f8950e7b1",
    };
    vga.set_color(0x02);
    for (int l = 0; l < 15; l++) {
        vga.set_xy(0, l);
        const char* r = rain[l];
        for (int i = 0; r[i] && i < 80; i++) vga.putChar(r[i]);
    }
    pitSleepMs(300);
    vga.set_color(0x00);
    vga.clear();

    // --- Phase 2: Static frame + header ---
    static const char bdr[] =
        "================================================================================";
    bootPut(vga, 0,  0, bdr, 0x0B);
    bootPut(vga, 0,  1, "  MrHakOS :: SECURE BOOT PROTOCOL v2.0", 0x0B);
    bootPut(vga, 46, 1, "[ CLASSIFIED - RESTRICTED ACCESS ]", 0x0E);
    bootPut(vga, 0,  2, bdr, 0x0B);

    // --- Phase 3: ASCII logo, line by line (bright green) ---
    static const char* logo[] = {
        "    __  __     _  _       _      ___  ____  ",
        "   |  \\/  |_ _| || | __ _| | __ / _ \\/ ___|",
        "   | |\\/| | '_|    |/ _` | |/ /| | | \\___ \\",
        "   | |  | | | | || | (_| |   < | |_| |___) |",
        "   |_|  |_|_| |_||_|\\__,_|_|\\_\\ \\___/|____/ ",
    };
    for (int i = 0; i < 5; i++) {
        bootPutCentered(vga, 4 + i, logo[i], 0x0A);
        pitSleepMs(120);
    }
    bootPutCentered(vga, 10,
        "< ENCRYPTED >  < FAIL-SAFE >  < TRUSTED >  < ZERO-TRUST >", 0x0E);

    bootPut(vga, 0,  11, bdr, 0x0B);
    bootPut(vga, 0,  12, "  >> SYSTEM INITIALIZATION LOG", 0x0B);
    bootPut(vga, 52, 12, "SESSION: 0xDEADBEEF <<  ", 0x0B);
    bootPut(vga, 0,  13, bdr, 0x0B);

    // --- Phase 4: Boot log entries ---
    static const char* labels[] = {
        " [0x00]  Memory guard armed",
        " [0x01]  Interrupt gate matrix sealed",
        " [0x02]  Encrypted filesystem mounted",
        " [0x03]  Network stack fail-closed",
        " [0x04]  Terminal sandbox activated",
    };
    for (int e = 0; e < 5; e++) {
        vga.set_color(0x0F);
        vga.set_xy(0, 15 + e);
        const char* lbl = labels[e];
        for (int i = 0; lbl[i]; i++) vga.putChar(lbl[i]);
        vga.set_color(0x08);
        int col = vga.get_x();
        for (; col < 69; col++) vga.putChar('.');
        pitSleepMs(1000);
        bootPut(vga, 70, 15 + e, "[  OK  ]", 0x0A);
        pitSleepMs(300);
    }

    // --- Phase 5: Progress bar ---
    static const char* pct_s[] = {
        " 10%", " 20%", " 30%", " 40%", " 50%",
        " 60%", " 70%", " 80%", " 90%", "100%"
    };
    bootPut(vga, 0,  21, " KERNEL BOOT: [", 0x0F);
    bootPut(vga, 62, 21, "]", 0x0F);
    for (int step = 0; step < 10; step++) {
        int filled = ((step + 1) * 46) / 10;
        for (int i = 0; i < 46; i++) {
            vga.set_color(i < filled ? 0x0A : 0x08);
            vga.putCharAt(i < filled ? '#' : '-', 15 + i, 21);
        }
        bootPut(vga, 63, 21, pct_s[step], 0x0A);
        pitSleepMs(100);
    }

    // --- Phase 6: ACCESS GRANTED banner ---
    bootPut(vga, 0, 22, bdr, 0x0B);
    bootPutCentered(vga, 23,
        ">> ACCESS GRANTED <<  WELCOME TO MrHakOS  |  KERNEL INTEGRITY: VERIFIED", 0x0A);
    bootPut(vga, 0, 24, bdr, 0x0B);
    pitSleepMs(800);

    vga.set_color(0x0F);
    vga.clear();
    vga.set_cursor_enabled(true);
}

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
    interrupts.enable();
    Serial::writeString("[kernel] interrupts enabled for boot animation timer\n");
    filesystem.init();
    Serial::writeString("[kernel] filesystem initialized\n");
    kernelNetwork.init();
    Serial::writeString("[kernel] network initialized\n");

    showBootAnimation(vga);

    terminal.init(&vga, &interrupts, &filesystem, &kernelNetwork);
    
    Serial::writeString("[kernel] entering terminal\n");
    terminal.run();
}
