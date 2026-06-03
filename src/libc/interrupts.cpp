#include <interrupts.hpp>
#include <stdint.h>
#include <io.hpp>
#include <serial.hpp>

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

// Keyboard character queue and state. The queue prevents quick USB legacy
// bursts from overwriting the single previous key before the terminal loop runs.
static volatile char key_queue[64];
static volatile uint8_t key_head = 0;
static volatile uint8_t key_tail = 0;
volatile bool shift_pressed = false;
static volatile bool extended_scancode = false;
static volatile uint32_t g_keyboard_irq_scancodes = 0;
static volatile uint32_t g_keyboard_polled_scancodes = 0;

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

// ISR symbols provided by assembly stubs
extern "C" void isr_timer();
extern "C" void isr_keyboard();

// Handlers array and APIs (shared interface)
typedef void (*handler_t)(void);
handler_t irq_handlers[16] = { 0 };
static volatile uint32_t g_timer_ticks = 0;
static const uint32_t PIT_HZ = 1000;
static uint16_t g_code_selector = 0x08;

static uint16_t currentCodeSelector() {
    uint16_t cs;
    asm volatile("mov %%cs, %0" : "=r"(cs));
    return cs;
}

Interrupts::Interrupts() {}

#if defined(__x86_64__)
static void setIDTEntry64(int index, void* handler, uint8_t flags) {
    uint64_t addr = (uint64_t)handler;
    idt64[index].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt64[index].selector    = g_code_selector; // current kernel code segment
    idt64[index].ist         = 0;        // no IST
    idt64[index].type_attr   = flags;    // 0x8E: present, int gate
    idt64[index].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt64[index].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt64[index].zero        = 0;
}
#else
static void setIDTEntry32(int index, uint32_t handler, uint8_t flags) {
    idt32[index].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt32[index].selector    = g_code_selector;
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
    // Enable IRQ0 (PIT timer) and IRQ1 (keyboard).
    outb(0x21, 0xFC);
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

static void initPIT() {
    uint32_t divisor = 1193182u / PIT_HZ;
    outb(0x43, 0x36); // channel 0, low/high byte, mode 3 square wave
    outb(0x40, static_cast<uint8_t>(divisor & 0xFF));
    outb(0x40, static_cast<uint8_t>((divisor >> 8) & 0xFF));
}

static void enqueueKey(char key) {
    if (key == 0) {
        return;
    }
    uint8_t next = static_cast<uint8_t>((key_head + 1) & 63);
    if (next == key_tail) {
        // Queue full: drop the newest key rather than corrupting the buffer.
        return;
    }
    key_queue[key_head] = key;
    key_head = next;
}

static bool processKeyboardScancode(uint8_t scancode) {
    if (scancode == 0xE0 || scancode == 0xE1) {
        extended_scancode = true;
        return false;
    }

    // Ignore extended key releases/non-text keys for now. This keeps arrow keys,
    // USB multimedia keys, etc. from turning into garbage characters.
    if (extended_scancode) {
        extended_scancode = false;
        return false;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return false;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
        return false;
    }
    if (scancode & 0x80) {
        return false;
    }

    char key = shift_pressed ? scancode_to_ascii_shifted[scancode]
                             : scancode_to_ascii[scancode];
    enqueueKey(key);
    return key != 0;
}

void Interrupts::init() {
    g_code_selector = currentCodeSelector();
    setupIDT();
    remapPIC();
#if defined(__x86_64__)
    setIDTEntry64(0x20, (void*)isr_timer, 0x8E);
    setIDTEntry64(0x21, (void*)isr_keyboard, 0x8E);
#else
    setIDTEntry32(0x20, (uint32_t)isr_timer, 0x8E);
    setIDTEntry32(0x21, (uint32_t)isr_keyboard, 0x8E);
#endif
    initPIT();
    loadIDT();
    // Do not enable interrupts here; let kernel enable after terminal init
}

// C++ handlers invoked by the ISR stubs
extern "C" void isr_timer_handler() {
    g_timer_ticks++;
    if (irq_handlers[0]) irq_handlers[0]();
    outb(0x20, 0x20);
}

extern "C" void isr_keyboard_handler() {
    uint8_t scancode = inb(0x60);
    g_keyboard_irq_scancodes++;
    if (processKeyboardScancode(scancode) && irq_handlers[1]) {
        Serial::writeString("[kbd] irq key\n");
        irq_handlers[1]();
    }
    outb(0x20, 0x20);
}

char getLastKey() {
    if (key_head == key_tail) {
        return 0;
    }
    char k = key_queue[key_tail];
    key_tail = static_cast<uint8_t>((key_tail + 1) & 63);
    return k;
}

bool pollKeyboard() {
    bool handled = false;
    // Drain a small burst. USB legacy emulation can deposit several translated
    // Set-1 scancodes without reliably raising IRQ1 after a GRUB boot.
    for (int i = 0; i < 16; i++) {
        uint8_t status = inb(0x64);
        if ((status & 0x01) == 0) {
            break;
        }
        // If AUX is set, this byte belongs to a PS/2 mouse; ignore it.
        if (status & 0x20) {
            (void)inb(0x60);
            continue;
        }
        uint8_t scancode = inb(0x60);
        g_keyboard_polled_scancodes++;
        if (processKeyboardScancode(scancode)) {
            Serial::writeString("[kbd] polled key\n");
            handled = true;
        }
    }
    if (handled && irq_handlers[1]) {
        irq_handlers[1]();
    }
    return handled;
}

uint32_t keyboardPolledScancodes() { return g_keyboard_polled_scancodes; }
uint32_t keyboardIrqScancodes() { return g_keyboard_irq_scancodes; }
uint32_t timerTicks() { return g_timer_ticks; }
uint32_t timerMillis() { return g_timer_ticks; }
void pitSleepMs(uint32_t ms) {
    uint32_t start = timerMillis();
    while (timerMillis() - start < ms) { asm volatile("hlt"); }
}
