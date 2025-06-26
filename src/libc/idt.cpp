#include <idt.hpp>

IDTEntry idt[256];
IDTDesc idtDescriptor;

void setIDTEntry(int index, void (*handler)(), uint8_t flags) {
    uint32_t addr = (uint32_t)handler;
    idt[index].offset_low = addr & 0xFFFF;
    idt[index].selector = 0x08; // Kernel code segment
    idt[index].zero = 0;
    idt[index].type_attr = flags;
    idt[index].offset_high = (addr >> 16) & 0xFFFF;
}
