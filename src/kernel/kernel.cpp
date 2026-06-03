// MrHakOS Kernel in C++
#include <stddef.h>
#include <string.hpp>
#include <vga.hpp>

#include <terminal.hpp>
#include <interrupts.hpp>
#include <filesystem.hpp>
#include <serial.hpp>
#include <network.hpp>

static Network kernelNetwork;

extern "C" void kernel_main() {
    Serial::init();
    Serial::writeString("[kernel] MrHakOS booting\n");

    Vga vga;
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