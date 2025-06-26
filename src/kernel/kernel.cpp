#include <stddef.h>
#include <string.hpp>
#include <vga.hpp>
#include <terminal.hpp>
#include <interrupts.hpp>

extern "C" void kernel_main() {
    // Initialize VGA console
    Vga vga;
    
    // Initialize terminal
    Terminal terminal;
    terminal.init(&vga);
    
    // Set up interrupt system
    Interrupts interrupts;
    interrupts.init();
    
    // Connect terminal to interrupt system
    terminal.setupInterrupts(&interrupts);
    
    // Display welcome message
    vga.set_color(0x0A); // Green on black
    terminal.putString("Welcome to MrHakOS with Keyboard Interrupt Support!\n");
    terminal.putString("Type 'help' for available commands\n\n");
    
    // Run the terminal
    terminal.run();
    
    // This code should never be reached due to the loop in terminal.run()
    while (true) {
        asm volatile("hlt");
    }
}