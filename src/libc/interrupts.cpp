#include <interrupts.hpp>
#include <stdint.h>
#include <io.hpp>

#if defined(__x86_64__)
// 64-bit IDT entry format
struct IDTEntry64 {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IDTDesc64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static IDTEntry64 idt64[256];
static IDTDesc64 idtDesc64;
#else
// 32-bit IDT entry format
struct IDTEntry32 {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct IDTDesc32 {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static IDTEntry32 idt32[256];
static IDTDesc32 idtDesc32;
#endif

// Keyboard scan code buffer and state
volatile char last_key = 0;
volatile bool shift_pressed = false;

// Keyboard scan code mapping (US layout) - Regular keys
const char scancode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Keyboard scan code mapping (US layout) - Shifted keys
const char scancode_to_ascii_shifted[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// ISR symbol provided by assembly stub
extern "C" void isr_keyboard();

// Handlers array and APIs (shared interface)
typedef void (*handler_t)(void);
handler_t irq_handlers[16] = { 0 };

Interrupts::Interrupts() {}

#if defined(__x86_64__)
static void setIDTEntry64(int index, void* handler, uint8_t flags) {
    uint64_t addr = (uint64_t)handler;
    idt64[index].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt64[index].selector    = 0x08;     // kernel code segment
    idt64[index].ist         = 0;        // no IST
    idt64[index].type_attr   = flags;    // 0x8E: present, int gate
    idt64[index].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt64[index].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt64[index].zero        = 0;
}
#else
static void setIDTEntry32(int index, uint32_t handler, uint8_t flags) {
    idt32[index].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt32[index].selector    = 0x08;
    idt32[index].zero        = 0;
    idt32[index].type_attr   = flags;    // 0x8E: present, int gate
    idt32[index].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}
#endif

void Interrupts::setupIDT() {
#if defined(__x86_64__)
    for (int i = 0; i < 256; i++) {
        setIDTEntry64(i, nullptr, 0);
    }
    idtDesc64.limit = sizeof(idt64) - 1;
    idtDesc64.base  = (uint64_t)&idt64;
#else
    for (int i = 0; i < 256; i++) {
        setIDTEntry32(i, 0, 0);
    }
    idtDesc32.limit = sizeof(idt32) - 1;
    idtDesc32.base  = (uint32_t)&idt32;
#endif
}

void Interrupts::remapPIC() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    // Enable only IRQ1 (keyboard)
    outb(0x21, 0xFD);
    outb(0xA1, 0xFF);
}

void Interrupts::loadIDT() {
#if defined(__x86_64__)
    asm volatile("lidt %0" : : "m"(idtDesc64));
#else
    asm volatile("lidt %0" : : "m"(idtDesc32));
#endif
}

void Interrupts::enable() { asm volatile("sti"); }
void Interrupts::disable() { asm volatile("cli"); }

void Interrupts::registerHandler(uint8_t irq, void (*handler)(void)) {
    irq_handlers[irq] = handler;
}

void Interrupts::init() {
    setupIDT();
    remapPIC();
#if defined(__x86_64__)
    setIDTEntry64(0x21, (void*)isr_keyboard, 0x8E);
#else
    setIDTEntry32(0x21, (uint32_t)isr_keyboard, 0x8E);
#endif
    loadIDT();
    // Do not enable interrupts here; let kernel enable after terminal init
}

// C++ handler invoked by the ISR stub
extern "C" void isr_keyboard_handler() {
    uint8_t scancode = inb(0x60);
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
    } else if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
    } else if (!(scancode & 0x80)) {
        last_key = shift_pressed ? scancode_to_ascii_shifted[scancode]
                                 : scancode_to_ascii[scancode];
        if (irq_handlers[1]) irq_handlers[1]();
    }
    outb(0x20, 0x20);
}

char getLastKey() { char k = last_key; last_key = 0; return k; }
