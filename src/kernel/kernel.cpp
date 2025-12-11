// MrHakOS Kernel in C++
#include <stddef.h>
#include <string.hpp>
#include <vga.hpp>

#include <terminal.hpp>
#include <interrupts.hpp>
#include <filesystem.hpp>

extern "C" void kernel_main() {
    Vga vga;

    Interrupts interrupts;
    FileSystem filesystem;
    Terminal terminal;
    
    interrupts.init();
    filesystem.init();

    terminal.init(&vga, &interrupts, &filesystem);
    
    interrupts.enable();
    terminal.run();
}