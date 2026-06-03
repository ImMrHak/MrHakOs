#include <serial.hpp>
#include <io.hpp>

uint16_t Serial::basePort = 0x3F8;
bool Serial::initialized = false;

static const char* HEX = "0123456789ABCDEF";

void Serial::init(uint16_t port) {
    basePort = port;

    // Disable interrupts while configuring COM1.
    outb(basePort + 1, 0x00);
    // Enable DLAB so divisor can be written.
    outb(basePort + 3, 0x80);
    // 38400 baud: divisor 3 for a 115200 baud base clock.
    outb(basePort + 0, 0x03);
    outb(basePort + 1, 0x00);
    // 8 data bits, no parity, one stop bit.
    outb(basePort + 3, 0x03);
    // Enable FIFO, clear RX/TX queues, 14-byte threshold.
    outb(basePort + 2, 0xC7);
    // IRQs enabled, RTS/DSR set.
    outb(basePort + 4, 0x0B);

    initialized = true;
    writeString("[serial] COM1 initialized\n");
}

bool Serial::isInitialized() {
    return initialized;
}

static bool serialTransmitEmpty(uint16_t port) {
    return (inb(port + 5) & 0x20) != 0;
}

void Serial::writeChar(char c) {
    if (!initialized) {
        return;
    }
    if (c == '\n') {
        writeChar('\r');
    }
    while (!serialTransmitEmpty(basePort)) {
        asm volatile("pause");
    }
    outb(basePort, static_cast<uint8_t>(c));
}

void Serial::writeString(const char* str) {
    if (!initialized || !str) {
        return;
    }
    for (int i = 0; str[i]; i++) {
        writeChar(str[i]);
    }
}

void Serial::writeHex8(uint8_t value) {
    writeChar(HEX[(value >> 4) & 0xF]);
    writeChar(HEX[value & 0xF]);
}

void Serial::writeHex16(uint16_t value) {
    writeHex8(static_cast<uint8_t>((value >> 8) & 0xFF));
    writeHex8(static_cast<uint8_t>(value & 0xFF));
}

void Serial::writeHex32(uint32_t value) {
    writeHex16(static_cast<uint16_t>((value >> 16) & 0xFFFF));
    writeHex16(static_cast<uint16_t>(value & 0xFFFF));
}
