#include <stddef.h>
#include <string.h>

extern "C" void kernel_main() {
    volatile unsigned short* vga_buffer = (unsigned short*)0xB8000;
    // 80 IS SCREEN WIDTH
    // 25 IS SCREEN HEIGHT
    for (int i = 0; i < 80 * 25; ++i) {
        vga_buffer[i] = (' ' | (0x0F << 8));
    }

    const char* message = "HI FROM CPP";
    int screen_pos = 0; // SCREEN POSITION X

    for (int i = 0; message[i] != '\0'; ++i) {
        // GREEN MESSAGE
        vga_buffer[screen_pos] = (message[i] | (0x0A << 8));
        screen_pos++;
    }

    // Hang forever so the message stays on screen.
    while (true) {
        asm volatile("hlt");
    }
}