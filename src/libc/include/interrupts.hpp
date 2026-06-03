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

char getLastKey();
// Poll the i8042/8042-compatible keyboard data port. This is important on
// real hardware/GRUB boots where USB legacy keyboard emulation may place USB
// HID keystrokes in the PS/2-compatible controller but IRQ1 is not delivered.
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