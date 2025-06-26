#pragma once
#include <stdint.h>

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct IDTDesc {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern IDTEntry idt[256];
extern IDTDesc idtDescriptor;

void setIDTEntry(int index, void (*handler)(), uint8_t flags);
