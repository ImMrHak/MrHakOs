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

#ifdef __cplusplus
}
#endif

#endif // _LIBC_INTERRUPTS_H