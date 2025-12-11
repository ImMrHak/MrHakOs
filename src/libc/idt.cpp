// 32-bit IDT helper (legacy)
#include <idt.hpp>

IDTEntry idt[256];
IDTDesc idtDescriptor;

void setIDTEntry(int index, void (*handler)(), uint8_t flags) {
    (void)index; (void)handler; (void)flags;
}