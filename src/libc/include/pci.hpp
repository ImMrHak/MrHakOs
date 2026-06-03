#ifndef PCI_HPP
#define PCI_HPP

#include <stdint.h>

struct PciDeviceInfo {
    bool present;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendorId;
    uint16_t deviceId;
    uint8_t classCode;
    uint8_t subclass;
    uint8_t progIf;
    uint8_t revisionId;
    uint8_t irqLine;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
};

class Pci {
public:
    static uint32_t readConfig32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
    static uint16_t readConfig16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
    static uint8_t readConfig8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
    static void writeConfig32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
    static void writeConfig16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
    static bool findDevice(uint16_t vendorId, uint16_t deviceId, PciDeviceInfo* outDevice);
    static bool findClass(uint8_t classCode, uint8_t subclass, PciDeviceInfo* outDevice);
    static void fillDeviceInfo(uint8_t bus, uint8_t device, uint8_t function, PciDeviceInfo* outDevice);
};

#endif // PCI_HPP
