#ifndef _LIBC_INTERRUPTS_H
#define _LIBC_INTERRUPTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

class Interrupts{
    private:
    public:
    // Constructor
    Interrupts();

    // Interrupts methods
    void init();
    void remapPIC();
    void loadIDT();
    void setupIDT();
    void enable();
    void disable();
    void registerHandler(uint8_t irq, void (*handler)(void));
};

// Special (non-ASCII) key codes delivered through the keyboard queue. They use
// otherwise-unused control byte values so they never collide with typed text.
enum SpecialKey {
    KEY_UP    = 0x11,
    KEY_DOWN  = 0x12,
    KEY_LEFT  = 0x13,
    KEY_RIGHT = 0x14,
    KEY_HOME  = 0x15,
    KEY_END   = 0x16,
    KEY_PGUP  = 0x17,
    KEY_PGDN  = 0x18,
    KEY_DEL   = 0x7F
};

char getLastKey();
// Poll the i8042/8042-compatible keyboard data port. This is important on
// real hardware/GRUB boots where USB legacy keyboard emulation may place USB
// HID keystrokes in the PS/2-compatible controller but IRQ1 is not delivered.
// No-op once a real IRQ1 has been seen, so the ISR and poller never both drain
// the same scancode (which previously produced duplicate characters).
bool pollKeyboard();
uint32_t keyboardPolledScancodes();
uint32_t keyboardIrqScancodes();
uint32_t timerTicks();
uint32_t timerMillis();
void pitSleepMs(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif // _LIBC_INTERRUPTS_H