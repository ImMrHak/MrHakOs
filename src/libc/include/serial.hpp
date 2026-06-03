#ifndef SERIAL_HPP
#define SERIAL_HPP

#include <stdint.h>

class Serial {
public:
    static void init(uint16_t port = 0x3F8);
    static bool isInitialized();
    static void writeChar(char c);
    static void writeString(const char* str);
    static void writeHex8(uint8_t value);
    static void writeHex16(uint16_t value);
    static void writeHex32(uint32_t value);

private:
    static uint16_t basePort;
    static bool initialized;
};

#endif // SERIAL_HPP
