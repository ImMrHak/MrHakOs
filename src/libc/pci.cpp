#include <pci.hpp>
#include <io.hpp>

static uint32_t pciAddress(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (1u << 31) |
           (static_cast<uint32_t>(bus) << 16) |
           (static_cast<uint32_t>(device & 0x1F) << 11) |
           (static_cast<uint32_t>(function & 0x07) << 8) |
           (offset & 0xFC);
}

uint32_t Pci::readConfig32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    outl(0xCF8, pciAddress(bus, device, function, offset));
    return inl(0xCFC);
}

uint16_t Pci::readConfig16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = readConfig32(bus, device, function, offset);
    return static_cast<uint16_t>((value >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t Pci::readConfig8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = readConfig32(bus, device, function, offset);
    return static_cast<uint8_t>((value >> ((offset & 3) * 8)) & 0xFF);
}

void Pci::writeConfig32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    outl(0xCF8, pciAddress(bus, device, function, offset));
    outl(0xCFC, value);
}

void Pci::writeConfig16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t current = readConfig32(bus, device, function, offset);
    uint32_t shift = (offset & 2) * 8;
    current &= ~(0xFFFFu << shift);
    current |= (static_cast<uint32_t>(value) << shift);
    writeConfig32(bus, device, function, offset, current);
}

void Pci::fillDeviceInfo(uint8_t bus, uint8_t device, uint8_t function, PciDeviceInfo* outDevice) {
    if (!outDevice) {
        return;
    }

    outDevice->present = true;
    outDevice->bus = bus;
    outDevice->device = device;
    outDevice->function = function;
    outDevice->vendorId = readConfig16(bus, device, function, 0x00);
    outDevice->deviceId = readConfig16(bus, device, function, 0x02);
    outDevice->revisionId = readConfig8(bus, device, function, 0x08);
    outDevice->progIf = readConfig8(bus, device, function, 0x09);
    outDevice->subclass = readConfig8(bus, device, function, 0x0A);
    outDevice->classCode = readConfig8(bus, device, function, 0x0B);
    outDevice->bar0 = readConfig32(bus, device, function, 0x10);
    outDevice->bar1 = readConfig32(bus, device, function, 0x14);
    outDevice->bar2 = readConfig32(bus, device, function, 0x18);
    outDevice->bar3 = readConfig32(bus, device, function, 0x1C);
    outDevice->bar4 = readConfig32(bus, device, function, 0x20);
    outDevice->bar5 = readConfig32(bus, device, function, 0x24);
    outDevice->irqLine = readConfig8(bus, device, function, 0x3C);
}

bool Pci::findDevice(uint16_t vendorId, uint16_t deviceId, PciDeviceInfo* outDevice) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t foundVendor = readConfig16(static_cast<uint8_t>(bus), device, function, 0x00);
                if (foundVendor == 0xFFFF) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                uint16_t foundDevice = readConfig16(static_cast<uint8_t>(bus), device, function, 0x02);
                if (foundVendor == vendorId && foundDevice == deviceId) {
                    fillDeviceInfo(static_cast<uint8_t>(bus), device, function, outDevice);
                    return true;
                }

                if (function == 0) {
                    uint8_t headerType = readConfig8(static_cast<uint8_t>(bus), device, function, 0x0E);
                    if ((headerType & 0x80) == 0) {
                        break;
                    }
                }
            }
        }
    }
    if (outDevice) {
        outDevice->present = false;
    }
    return false;
}

bool Pci::findClass(uint8_t classCode, uint8_t subclass, PciDeviceInfo* outDevice) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t foundVendor = readConfig16(static_cast<uint8_t>(bus), device, function, 0x00);
                if (foundVendor == 0xFFFF) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                uint8_t foundClass = readConfig8(static_cast<uint8_t>(bus), device, function, 0x0B);
                uint8_t foundSubclass = readConfig8(static_cast<uint8_t>(bus), device, function, 0x0A);
                if (foundClass == classCode && foundSubclass == subclass) {
                    fillDeviceInfo(static_cast<uint8_t>(bus), device, function, outDevice);
                    return true;
                }

                if (function == 0) {
                    uint8_t headerType = readConfig8(static_cast<uint8_t>(bus), device, function, 0x0E);
                    if ((headerType & 0x80) == 0) {
                        break;
                    }
                }
            }
        }
    }
    if (outDevice) {
        outDevice->present = false;
    }
    return false;
}
