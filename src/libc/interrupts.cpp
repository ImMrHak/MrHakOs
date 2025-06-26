#include <interrupts.hpp>
#include <idt.hpp>
#include <io.hpp>

// Keyboard scan code buffer
volatile char last_key = 0;

// Keyboard scan code mapping (US layout)
const char scancode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Function pointer type for handlers
typedef void (*handler_t)(void);

// Handler pointers array
handler_t irq_handlers[16] = { 0 };

// Forward declarations for interrupt handlers
extern "C" void isr_keyboard();

// Keyboard IRQ handler (IRQ1)
extern "C" void _Z19isr_keyboard_handlerv() {
    // Read scan code from keyboard
    uint8_t scancode = inb(0x60);
    
    // Only process key down events (not key up)
    if (!(scancode & 0x80)) {
        last_key = scancode_to_ascii[scancode];
        
        // Call registered handler if available
        if (irq_handlers[1]) {
            irq_handlers[1]();
        }
    }
    
    // Send EOI (End of Interrupt) to PIC
    outb(0x20, 0x20);
}

// Implementations for Interrupts class
Interrupts::Interrupts() {
    // Initialize handler pointers to nullptr
    for (int i = 0; i < 16; i++) {
        irq_handlers[i] = nullptr;
    }
}

void Interrupts::init() {
    // Setup the IDT
    setupIDT();
    
    // Remap PIC
    remapPIC();
    
    // Setup keyboard interrupt (IRQ1)
    setIDTEntry(0x21, (void(*)())isr_keyboard, 0x8E); // Present, Ring0, Interrupt Gate
    
    // Load IDT
    loadIDT();
    
    // Enable interrupts
    enable();
}

void Interrupts::remapPIC() {
    // Remap the PIC so that IRQs start at 0x20 (32)
    // ICW1
    outb(0x20, 0x11); // Initialize master PIC
    outb(0xA0, 0x11); // Initialize slave PIC
    
    // ICW2
    outb(0x21, 0x20); // Master PIC vector offset (IRQ0->IRQ7 = INT 0x20->0x27)
    outb(0xA1, 0x28); // Slave PIC vector offset (IRQ8->IRQ15 = INT 0x28->0x2F)
    
    // ICW3
    outb(0x21, 0x04); // Tell master PIC that slave PIC is at IRQ2
    outb(0xA1, 0x02); // Tell slave PIC its cascade identity
    
    // ICW4
    outb(0x21, 0x01); // 8086 mode for master
    outb(0xA1, 0x01); // 8086 mode for slave
    
    // Enable only the keyboard interrupt (IRQ1)
    outb(0x21, 0xFD); // 11111101 - Enable IRQ1
    outb(0xA1, 0xFF); // All disabled on slave PIC
}

void Interrupts::loadIDT() {
    // Load IDT using assembly
    asm volatile("lidt %0" : : "m"(idtDescriptor));
}

void Interrupts::setupIDT() {
    // Initialize IDT table with zeros
    for (int i = 0; i < 256; i++) {
        setIDTEntry(i, nullptr, 0);
    }
    
    // Initialize IDT descriptor
    idtDescriptor.limit = (sizeof(IDTEntry) * 256) - 1;
    idtDescriptor.base = (uint32_t)&idt;
}

void Interrupts::enable() {
    // Enable interrupts (clear interrupt flag)
    asm volatile("sti");
}

void Interrupts::disable() {
    // Disable interrupts (set interrupt flag)
    asm volatile("cli");
}

void Interrupts::registerHandler(uint8_t irq, void (*handler)(void)) {
    // Store handler in array
    irq_handlers[irq] = handler;
}

// Function to get the last pressed key
char getLastKey() {
    char key = last_key;
    last_key = 0; // Reset after reading
    return key;
}

// ASM interrupt handlers
extern "C" void isr_keyboard() {
    // Save registers
    asm volatile(
        "pushal\n"
        "call _Z19isr_keyboard_handlerv\n"
        "popal\n"
        "iret"
    );
}
