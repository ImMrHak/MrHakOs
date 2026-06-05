#include <network.hpp>
#include <serial.hpp>
#include <string.hpp>
#include <io.hpp>
#include <interrupts.hpp>

static const uint16_t RTL8139_VENDOR = 0x10EC;
static const uint16_t RTL8139_DEVICE = 0x8139;
static const uint16_t RTL8168_DEVICE = 0x8168;
static const uint16_t RTL8169_DEVICE = 0x8169;
static const uint16_t RTL8161_DEVICE = 0x8161;
static const char* HEX = "0123456789ABCDEF";

static const uint32_t DEFAULT_IP      = 0u;
static const uint32_t DEFAULT_GATEWAY = 0u;
static const uint32_t DEFAULT_DNS     = 0u;
static const uint32_t DEFAULT_NETMASK = 0u;

static const uint16_t ETH_ARP  = 0x0806;
static const uint16_t ETH_IPV4 = 0x0800;
static const uint8_t BROADCAST_MAC[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
static const uint32_t BROADCAST_IP = 0xffffffffu;
static const uint32_t ZERO_IP = 0u;

static uint8_t rtlRxBuffer[8192 + 16 + 1500] __attribute__((aligned(16)));
static uint8_t txFrame[1536] __attribute__((aligned(4)));

static const uint8_t NIC_NONE = 0;
static const uint8_t NIC_RTL8139 = 1;
static const uint8_t NIC_RTL8169 = 2;
static const uint32_t RTL_DESC_OWN = 0x80000000u;
static const uint32_t RTL_DESC_EOR = 0x40000000u;
static const uint32_t RTL_DESC_FS  = 0x20000000u;
static const uint32_t RTL_DESC_LS  = 0x10000000u;
static const uint16_t RTL8169_RX_DESC_COUNT = 16;
static const uint16_t RTL8169_TX_DESC_COUNT = 4;
static const uint16_t RTL8169_RX_BUF_SIZE = 2048;

static bool rtl8169UseMmio = false;
static uintptr_t rtl8169RegBase = 0;

static uint8_t rtl8169Read8(uint16_t off) {
    if (rtl8169UseMmio) return *reinterpret_cast<volatile uint8_t*>(rtl8169RegBase + off);
    return inb(static_cast<uint16_t>(rtl8169RegBase + off));
}

static uint16_t rtl8169Read16(uint16_t off) {
    if (rtl8169UseMmio) return *reinterpret_cast<volatile uint16_t*>(rtl8169RegBase + off);
    return inw(static_cast<uint16_t>(rtl8169RegBase + off));
}

static uint32_t rtl8169Read32(uint16_t off) {
    if (rtl8169UseMmio) return *reinterpret_cast<volatile uint32_t*>(rtl8169RegBase + off);
    return inl(static_cast<uint16_t>(rtl8169RegBase + off));
}

static void rtl8169Write8(uint16_t off, uint8_t v) {
    if (rtl8169UseMmio) *reinterpret_cast<volatile uint8_t*>(rtl8169RegBase + off) = v;
    else outb(static_cast<uint16_t>(rtl8169RegBase + off), v);
}

static void rtl8169Write16(uint16_t off, uint16_t v) {
    if (rtl8169UseMmio) *reinterpret_cast<volatile uint16_t*>(rtl8169RegBase + off) = v;
    else outw(static_cast<uint16_t>(rtl8169RegBase + off), v);
}

static void rtl8169Write32(uint16_t off, uint32_t v) {
    if (rtl8169UseMmio) *reinterpret_cast<volatile uint32_t*>(rtl8169RegBase + off) = v;
    else outl(static_cast<uint16_t>(rtl8169RegBase + off), v);
}

struct Rtl8169Desc {
    volatile uint32_t command;
    volatile uint32_t vlan;
    volatile uint32_t lowBuf;
    volatile uint32_t highBuf;
} __attribute__((packed));

static Rtl8169Desc rtl8169RxDesc[RTL8169_RX_DESC_COUNT] __attribute__((aligned(256)));
static Rtl8169Desc rtl8169TxDesc[RTL8169_TX_DESC_COUNT] __attribute__((aligned(256)));
static uint8_t rtl8169RxBuffers[RTL8169_RX_DESC_COUNT][RTL8169_RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t rtl8169TxBuffers[RTL8169_TX_DESC_COUNT][1536] __attribute__((aligned(16)));

static void wr16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }
static void wr32(uint8_t* p, uint32_t v) { p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16); p[2] = static_cast<uint8_t>(v >> 8); p[3] = static_cast<uint8_t>(v); }
static uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3]; }

static bool sameSubnet(uint32_t a, uint32_t b, uint32_t mask) { return mask != 0 && (a & mask) == (b & mask); }

static uint32_t defaultNetmaskForIp(uint32_t ip) {
    uint8_t first = static_cast<uint8_t>(ip >> 24);
    if (first < 128) return 0xff000000u;
    if (first < 192) return 0xffff0000u;
    return 0xffffff00u;
}

static uint16_t tcpChecksum(uint32_t srcIp, uint32_t dstIp, const uint8_t* tcp, uint16_t tcpLen) {
    uint32_t sum = 0;
    sum += static_cast<uint16_t>(srcIp >> 16); sum += static_cast<uint16_t>(srcIp);
    sum += static_cast<uint16_t>(dstIp >> 16); sum += static_cast<uint16_t>(dstIp);
    sum += 6; sum += tcpLen;
    for (uint16_t i = 0; i + 1 < tcpLen; i += 2) sum += static_cast<uint16_t>((tcp[i] << 8) | tcp[i + 1]);
    if (tcpLen & 1) sum += static_cast<uint16_t>(tcp[tcpLen - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

static uint16_t checksum(const uint8_t* data, uint16_t len) {
    uint32_t sum = 0;
    for (uint16_t i = 0; i + 1 < len; i += 2) {
        sum += static_cast<uint16_t>((data[i] << 8) | data[i + 1]);
    }
    if (len & 1) {
        sum += static_cast<uint16_t>(data[len - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

Network::Network() {
    clearInfo();
    pciDevice.present = false;
    rxBufferPhys = 0;
    rxOffset = 0;
    txIndex = 0;
    arpValid = false;
    nextIcmpSeq = 1;
    lastEchoId = 0x484B; // HK
    lastEchoSeq = 0;
    lastEchoReplyIp = 0;
    lastEchoReplyTtl = 0;
    echoReplySeen = false;
    icmpTimeExceededSeen = false;
    lastIcmpErrorIp = 0;
    lastIcmpErrorSeq = 0;
    dnsReplySeen = false;
    lastDnsId = 0;
    lastDnsIp = 0;
    lastTcpSourcePort = 0;
    lastTcpDestPort = 0;
    lastTcpRemoteIp = 0;
    lastTcpSeq = 0;
    lastTcpAck = 0;
    tcpSynAckSeen = false;
    tcpFinSeen = false;
    tcpRstSeen = false;
    tcpDataSeen = false;
    tcpRxLen = 0;
    tcpConsumed = 0;
    tcpRxBuffer[0] = 0;
    dhcpOfferSeen = false;
    dhcpAckSeen = false;
    dhcpXid = 0;
    dhcpOfferedIp = 0;
    dhcpServerIp = 0;
    dhcpRouterIp = 0;
    dhcpDnsIp = 0;
    dhcpNetmask = 0;
    dhcpState = 0;
    dhcpStateStartMs = 0;
    nicKind = NIC_NONE;
}

void Network::clearInfo() {
    info.pciPresent = false;
    info.rtl8139Present = false;
    info.linkUp = false;
    info.rxEnabled = false;
    info.nicName[0] = '\0';
    for (int i = 0; i < 6; i++) info.mac[i] = 0;
    info.ioBase = 0;
    info.irqLine = 0;
    info.ipAddress = DEFAULT_IP;
    info.gatewayIp = DEFAULT_GATEWAY;
    info.dnsIp = DEFAULT_DNS;
    info.netmask = DEFAULT_NETMASK;
    info.rxPackets = 0;
    info.txPackets = 0;
    info.arpPackets = 0;
    info.ipv4Packets = 0;
    info.icmpPackets = 0;
    info.dhcpConfigured = false;
    dhcpState = 0;
    dhcpStateStartMs = 0;
    nicKind = NIC_NONE;
}

void Network::init() {
    Serial::writeString("[net] init start\n");
    clearInfo();
    Serial::writeString("[net] after clearInfo\n");
    Serial::writeString("[net] scanning PCI bus\n");

    PciDeviceInfo device;
    if (Pci::findDevice(RTL8139_VENDOR, RTL8139_DEVICE, &device)) {
        info.pciPresent = true;
        pciDevice = device;
        initRtl8139(device);
        return;
    }
    if (Pci::findDevice(RTL8139_VENDOR, RTL8168_DEVICE, &device) ||
        Pci::findDevice(RTL8139_VENDOR, RTL8169_DEVICE, &device) ||
        Pci::findDevice(RTL8139_VENDOR, RTL8161_DEVICE, &device)) {
        info.pciPresent = true;
        pciDevice = device;
        initRtl8169(device);
        return;
    }

    if (Pci::findClass(0x02, 0x00, &device)) {
        info.pciPresent = true;
        pciDevice = device;
        info.ioBase = device.bar0;
        info.irqLine = device.irqLine;
        const char name[] = "Ethernet";
        int i = 0; for (; name[i] && i < 31; i++) info.nicName[i] = name[i]; info.nicName[i] = 0;
        Serial::writeString("[net] found unsupported ethernet PCI device vendor=0x");
        Serial::writeHex16(device.vendorId);
        Serial::writeString(" device=0x");
        Serial::writeHex16(device.deviceId);
        Serial::writeString("\n");
        return;
    }

    Serial::writeString("[net] no PCI ethernet NIC detected\n");
}

void Network::initRtl8139(const PciDeviceInfo& device) {
    nicKind = NIC_RTL8139;
    info.rtl8139Present = true;
    info.ioBase = device.bar0 & 0xFFFFFFFC;
    info.irqLine = device.irqLine;
    const char name[] = "RTL8139";
    int i = 0; for (; name[i] && i < 31; i++) info.nicName[i] = name[i]; info.nicName[i] = 0;

    // Enable PCI I/O space, memory space, and bus mastering.
    uint16_t cmd = Pci::readConfig16(device.bus, device.device, device.function, 0x04);
    cmd |= 0x0007;
    Pci::writeConfig16(device.bus, device.device, device.function, 0x04, cmd);

    if (info.ioBase != 0) {
        // Power on, reset, and set RX buffer.
        outb(static_cast<uint16_t>(info.ioBase + 0x52), 0x00);
        outb(static_cast<uint16_t>(info.ioBase + 0x37), 0x10);
        for (int spin = 0; spin < 100000; spin++) {
            if ((inb(static_cast<uint16_t>(info.ioBase + 0x37)) & 0x10) == 0) break;
        }

        rxBufferPhys = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rtlRxBuffer));
        rxOffset = 0;
        txIndex = 0;
        memset(rtlRxBuffer, 0, sizeof(rtlRxBuffer));
        outl(static_cast<uint16_t>(info.ioBase + 0x30), rxBufferPhys);

        // Accept broadcast, physical-match, and ARP/IP multicast-ish packets.
        outl(static_cast<uint16_t>(info.ioBase + 0x44), 0x00000F | (1 << 7));
        outl(static_cast<uint16_t>(info.ioBase + 0x40), 0x03000700);
        outw(static_cast<uint16_t>(info.ioBase + 0x3C), 0x0000); // poll for now, no IRQs
        outw(static_cast<uint16_t>(info.ioBase + 0x3E), 0xFFFF); // clear pending status
        outb(static_cast<uint16_t>(info.ioBase + 0x37), 0x0C);   // RE | TE

        for (int m = 0; m < 6; m++) info.mac[m] = inb(static_cast<uint16_t>(info.ioBase + m));
        uint8_t mediaStatus = inb(static_cast<uint16_t>(info.ioBase + 0x58));
        info.linkUp = (mediaStatus & 0x04) == 0;
        info.rxEnabled = true;
    }

    Serial::writeString("[net] RTL8139 detected at PCI ");
    Serial::writeHex8(device.bus); Serial::writeChar(':'); Serial::writeHex8(device.device); Serial::writeChar('.'); Serial::writeHex8(device.function);
    Serial::writeString(" io=0x"); Serial::writeHex32(info.ioBase);
    Serial::writeString(" irq="); Serial::writeHex8(info.irqLine);
    Serial::writeString(" mac="); char macText[18]; formatMac(macText, sizeof(macText)); Serial::writeString(macText);
    Serial::writeString(" ip="); char ipText[16]; formatIp(info.ipAddress, ipText, sizeof(ipText)); Serial::writeString(ipText);
    Serial::writeString(" dns="); formatIp(info.dnsIp, ipText, sizeof(ipText)); Serial::writeString(ipText);
    Serial::writeString("\n");
}

void Network::initRtl8169(const PciDeviceInfo& device) {
    nicKind = NIC_RTL8169;
    info.rtl8139Present = true; // Existing higher network stack treats this as "supported Realtek NIC".
    info.ioBase = device.bar0 & 0xFFFFFFFC;
    info.irqLine = device.irqLine;
    const char name[] = "RTL8168/8169";
    int i = 0; for (; name[i] && i < 31; i++) info.nicName[i] = name[i]; info.nicName[i] = 0;

    rtl8169UseMmio = false;
    rtl8169RegBase = 0;
    uint32_t bars[6] = { device.bar0, device.bar1, device.bar2, device.bar3, device.bar4, device.bar5 };

    // Real RTL8111/8168 cards can expose their usable register window on
    // different BARs depending on BIOS/firmware. Prefer I/O space because this
    // kernel has no paging/MMIO mapping layer yet, then fall back to MMIO.
    for (int b = 0; b < 6 && rtl8169RegBase == 0; b++) {
        if (bars[b] != 0 && bars[b] != 0xffffffffu && (bars[b] & 0x1)) {
            rtl8169RegBase = static_cast<uintptr_t>(bars[b] & 0xFFFFFFFCu);
            rtl8169UseMmio = false;
        }
    }
    for (int b = 0; b < 6 && rtl8169RegBase == 0; b++) {
        if (bars[b] != 0 && bars[b] != 0xffffffffu && (bars[b] & 0x1) == 0) {
            rtl8169RegBase = static_cast<uintptr_t>(bars[b] & 0xFFFFFFF0u);
            rtl8169UseMmio = true;
        }
    }
    if (rtl8169RegBase == 0) {
        Serial::writeString("[net] RTL8168/8169 found but no usable I/O or MMIO BAR is enabled\n");
        return;
    }
    info.ioBase = static_cast<uint32_t>(rtl8169RegBase);

    uint16_t cmd = Pci::readConfig16(device.bus, device.device, device.function, 0x04);
    cmd |= 0x0007; // I/O space | memory space | bus mastering
    Pci::writeConfig16(device.bus, device.device, device.function, 0x04, cmd);

    // Wake the chip and unlock config registers. Some real RTL8168 revisions
    // stay in low-power mode after firmware unless Config1 PM bits are cleared.
    rtl8169Write8(0x50, 0xC0);
    rtl8169Write8(0x52, 0x00);
    rtl8169Write8(0x37, 0x10); // reset
    for (int spin = 0; spin < 1000000; spin++) {
        if ((rtl8169Read8(0x37) & 0x10) == 0) break;
    }

    for (int m = 0; m < 6; m++) info.mac[m] = rtl8169Read8(static_cast<uint16_t>(m));

    memset(rtl8169RxBuffers, 0, sizeof(rtl8169RxBuffers));
    memset(rtl8169TxBuffers, 0, sizeof(rtl8169TxBuffers));
    memset(rtl8169RxDesc, 0, sizeof(rtl8169RxDesc));
    memset(rtl8169TxDesc, 0, sizeof(rtl8169TxDesc));
    txIndex = 0;
    rxOffset = 0;

    for (uint16_t r = 0; r < RTL8169_RX_DESC_COUNT; r++) {
        rtl8169RxDesc[r].command = RTL_DESC_OWN | (r == RTL8169_RX_DESC_COUNT - 1 ? RTL_DESC_EOR : 0) | RTL8169_RX_BUF_SIZE;
        rtl8169RxDesc[r].vlan = 0;
        rtl8169RxDesc[r].lowBuf = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rtl8169RxBuffers[r]));
        rtl8169RxDesc[r].highBuf = 0;
    }
    for (uint16_t t = 0; t < RTL8169_TX_DESC_COUNT; t++) {
        rtl8169TxDesc[t].command = (t == RTL8169_TX_DESC_COUNT - 1 ? RTL_DESC_EOR : 0);
        rtl8169TxDesc[t].vlan = 0;
        rtl8169TxDesc[t].lowBuf = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rtl8169TxBuffers[t]));
        rtl8169TxDesc[t].highBuf = 0;
    }

    rtl8169Write8(0x50, 0xC0);        // unlock config regs
    rtl8169Write16(0x3C, 0x0000);     // poll for now, no IRQs/MSI
    rtl8169Write16(0x3E, 0xFFFF);     // clear pending status
    rtl8169Write32(0x44, 0x0000E70F); // accept broadcast/multicast/physical match, unlimited DMA/FIFO
    rtl8169Write32(0x40, 0x03000700); // normal IFG, unlimited DMA
    rtl8169Write16(0xDA, 0x1FFF);     // receive max size
    rtl8169Write8(0xEC, 0x3B);        // max transmit packet size
    rtl8169Write16(0xE0, static_cast<uint16_t>(rtl8169Read16(0xE0) & ~0x0010u)); // keep 32-bit descriptor mode
    rtl8169Write32(0x20, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rtl8169TxDesc)));
    rtl8169Write32(0x24, 0);
    rtl8169Write32(0xE4, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rtl8169RxDesc)));
    rtl8169Write32(0xE8, 0);
    rtl8169Write8(0x37, 0x0C);        // enable RX | TX after rings are installed
    rtl8169Write16(0x3E, 0xFFFF);     // clear pending status after enabling
    rtl8169Write8(0x50, 0x00);        // lock config regs

    uint8_t phyStatus = rtl8169Read8(0x6C);
    for (int spin = 0; spin < 2000000 && (phyStatus & 0x02) == 0; spin++) {
        phyStatus = rtl8169Read8(0x6C);
    }
    info.linkUp = (phyStatus & 0x02) != 0;
    info.rxEnabled = true;

    Serial::writeString("[net] RTL8168/8169 detected at PCI ");
    Serial::writeHex8(device.bus); Serial::writeChar(':'); Serial::writeHex8(device.device); Serial::writeChar('.'); Serial::writeHex8(device.function);
    Serial::writeString(rtl8169UseMmio ? " mmio=0x" : " io=0x"); Serial::writeHex32(info.ioBase);
    Serial::writeString(" bar2=0x"); Serial::writeHex32(device.bar2);
    Serial::writeString(" bar4=0x"); Serial::writeHex32(device.bar4);
    Serial::writeString(" irq="); Serial::writeHex8(info.irqLine);
    Serial::writeString(" cmd=0x"); Serial::writeHex8(rtl8169Read8(0x37));
    Serial::writeString(" isr=0x"); Serial::writeHex16(rtl8169Read16(0x3E));
    Serial::writeString(" phy=0x"); Serial::writeHex8(rtl8169Read8(0x6C));
    Serial::writeString(" mac="); char macText[18]; formatMac(macText, sizeof(macText)); Serial::writeString(macText);
    Serial::writeString(" ip="); char ipText[16]; formatIp(info.ipAddress, ipText, sizeof(ipText)); Serial::writeString(ipText);
    Serial::writeString("\n");
}

const NetworkInfo& Network::getInfo() const { return info; }

void Network::formatMac(char* out, int outLen) const {
    if (!out || outLen <= 0) return;
    int p = 0;
    for (int i = 0; i < 6; i++) {
        if (p + 2 >= outLen) break;
        out[p++] = HEX[(info.mac[i] >> 4) & 0xF]; out[p++] = HEX[info.mac[i] & 0xF];
        if (i != 5 && p + 1 < outLen) out[p++] = ':';
    }
    if (p >= outLen) {
        p = outLen - 1;
    }
    out[p] = 0;
}

void Network::formatIp(uint32_t ip, char* out, int outLen) const {
    if (!out || outLen <= 0) return;
    uint8_t b[4] = { static_cast<uint8_t>(ip >> 24), static_cast<uint8_t>(ip >> 16), static_cast<uint8_t>(ip >> 8), static_cast<uint8_t>(ip) };
    int p = 0;
    for (int i = 0; i < 4; i++) {
        if (b[i] >= 100 && p < outLen - 1) out[p++] = static_cast<char>('0' + b[i] / 100);
        if (b[i] >= 10 && p < outLen - 1) out[p++] = static_cast<char>('0' + (b[i] / 10) % 10);
        if (p < outLen - 1) out[p++] = static_cast<char>('0' + b[i] % 10);
        if (i != 3 && p < outLen - 1) out[p++] = '.';
    }
    out[p < outLen ? p : outLen - 1] = 0;
}

bool Network::parseIp(const char* text, uint32_t* outIp) const {
    if (!text || !outIp) return false;
    uint32_t parts[4] = {0,0,0,0}; int part = 0; bool haveDigit = false;
    for (int i = 0;; i++) {
        char c = text[i];
        if (c >= '0' && c <= '9') { haveDigit = true; parts[part] = parts[part] * 10 + static_cast<uint32_t>(c - '0'); if (parts[part] > 255) return false; }
        else if (c == '.' && haveDigit && part < 3) { part++; haveDigit = false; }
        else if ((c == 0 || c == ' ') && haveDigit && part == 3) { *outIp = (parts[0]<<24)|(parts[1]<<16)|(parts[2]<<8)|parts[3]; return true; }
        else return false;
    }
}

bool Network::sendFrame(const uint8_t* destMac, uint16_t etherType, const uint8_t* payload, uint16_t payloadLen) {
    if (nicKind == NIC_RTL8169) return sendFrameRtl8169(destMac, etherType, payload, payloadLen);
    return sendFrameRtl8139(destMac, etherType, payload, payloadLen);
}

bool Network::sendFrameRtl8139(const uint8_t* destMac, uint16_t etherType, const uint8_t* payload, uint16_t payloadLen) {
    if (!info.rtl8139Present || !info.rxEnabled || static_cast<uint32_t>(payloadLen) + 14u > sizeof(txFrame)) return false;
    memcpy(txFrame, destMac, 6);
    memcpy(txFrame + 6, info.mac, 6);
    wr16(txFrame + 12, etherType);
    memcpy(txFrame + 14, payload, payloadLen);
    uint16_t frameLen = payloadLen + 14;
    if (frameLen < 60) { memset(txFrame + frameLen, 0, 60 - frameLen); frameLen = 60; }

    uint16_t txBase = static_cast<uint16_t>(info.ioBase + 0x10 + txIndex * 4);
    uint16_t addrBase = static_cast<uint16_t>(info.ioBase + 0x20 + txIndex * 4);
    outl(addrBase, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(txFrame)));
    outl(txBase, frameLen);

    bool ok = false;
    for (int spin = 0; spin < 100000; spin++) {
        uint32_t status = inl(txBase);
        if (status & (1 << 15)) { ok = true; break; }
        if (status & (1 << 14)) { break; }
    }
    txIndex = static_cast<uint8_t>((txIndex + 1) & 3);
    info.txPackets++;
    (void)ok;
    return true;
}

bool Network::sendFrameRtl8169(const uint8_t* destMac, uint16_t etherType, const uint8_t* payload, uint16_t payloadLen) {
    if (!info.rtl8139Present || !info.rxEnabled || static_cast<uint32_t>(payloadLen) + 14u > 1536u) return false;

    Rtl8169Desc& desc = rtl8169TxDesc[txIndex];
    for (int spin = 0; spin < 100000; spin++) {
        if ((desc.command & RTL_DESC_OWN) == 0) break;
    }
    if (desc.command & RTL_DESC_OWN) return false;

    uint8_t* frame = rtl8169TxBuffers[txIndex];
    memcpy(frame, destMac, 6);
    memcpy(frame + 6, info.mac, 6);
    wr16(frame + 12, etherType);
    memcpy(frame + 14, payload, payloadLen);
    uint16_t frameLen = static_cast<uint16_t>(payloadLen + 14);
    if (frameLen < 60) { memset(frame + frameLen, 0, 60 - frameLen); frameLen = 60; }

    uint32_t eor = (txIndex == RTL8169_TX_DESC_COUNT - 1) ? RTL_DESC_EOR : 0;
    desc.vlan = 0;
    desc.lowBuf = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame));
    desc.highBuf = 0;
    desc.command = RTL_DESC_OWN | RTL_DESC_FS | RTL_DESC_LS | eor | frameLen;
    rtl8169Write8(0x38, 0x40); // normal-priority TX poll

    bool ok = false;
    for (int spin = 0; spin < 300000; spin++) {
        if ((desc.command & RTL_DESC_OWN) == 0) { ok = true; break; }
    }
    txIndex = static_cast<uint8_t>((txIndex + 1) % RTL8169_TX_DESC_COUNT);
    info.txPackets++;
    return ok;
}

void Network::sendArpRequest(uint32_t targetIp) {
    uint8_t p[28];
    wr16(p + 0, 1); wr16(p + 2, ETH_IPV4); p[4] = 6; p[5] = 4; wr16(p + 6, 1);
    memcpy(p + 8, info.mac, 6); wr32(p + 14, info.ipAddress);
    memset(p + 18, 0, 6); wr32(p + 24, targetIp);
    sendFrame(BROADCAST_MAC, ETH_ARP, p, sizeof(p));
    Serial::writeString("[net] ARP who-has sent for ");
    char ipText[16];
    formatIp(targetIp, ipText, sizeof(ipText));
    Serial::writeString(ipText);
    Serial::writeString("\n");
}

bool Network::lookupArp(uint32_t ip, uint8_t* outMac) const {
    if (!arpValid || rd32(arpIp) != ip) return false;
    memcpy(outMac, arpMac, 6);
    return true;
}

void Network::rememberArp(uint32_t ip, const uint8_t* mac) {
    wr32(arpIp, ip); memcpy(arpMac, mac, 6); arpValid = true;
}

uint32_t Network::routeNextHop(uint32_t targetIp) const {
    if (targetIp == 0 || targetIp == BROADCAST_IP) return targetIp;
    if (info.gatewayIp != 0) {
        if (targetIp == info.gatewayIp) return targetIp;
        if (info.netmask == 0) return info.gatewayIp;
        if (!sameSubnet(targetIp, info.ipAddress, info.netmask)) return info.gatewayIp;
    }
    return targetIp;
}

bool Network::sendIcmpEcho(uint32_t targetIp, const uint8_t* destMac) {
    return sendIcmpEchoWithTtl(targetIp, destMac, 64, nextIcmpSeq++);
}

bool Network::sendIcmpEchoWithTtl(uint32_t targetIp, const uint8_t* destMac, uint8_t ttl, uint16_t sequence) {
    uint8_t p[20 + 8 + 8];
    memset(p, 0, sizeof(p));
    p[0] = 0x45; p[1] = 0; wr16(p + 2, sizeof(p)); wr16(p + 4, sequence); wr16(p + 6, 0); p[8] = ttl; p[9] = 1;
    wr32(p + 12, info.ipAddress); wr32(p + 16, targetIp); wr16(p + 10, checksum(p, 20));
    p[20] = 8; p[21] = 0; wr16(p + 24, lastEchoId); lastEchoSeq = sequence; wr16(p + 26, lastEchoSeq);
    p[28] = 'M'; p[29] = 'r'; p[30] = 'H'; p[31] = 'a'; p[32] = 'k'; p[33] = 'O'; p[34] = 'S'; p[35] = 0;
    wr16(p + 22, checksum(p + 20, 16));
    echoReplySeen = false;
    icmpTimeExceededSeen = false;
    lastEchoReplyIp = 0;
    lastEchoReplyTtl = 0;
    lastIcmpErrorIp = 0;
    lastIcmpErrorSeq = sequence;
    bool ok = sendFrame(destMac, ETH_IPV4, p, sizeof(p));
    if (ok) Serial::writeString("[net] ICMP echo sent\n");
    return ok;
}

void Network::poll() {
    if (nicKind == NIC_RTL8169) { pollRtl8169(); return; }
    pollRtl8139();
}

void Network::pollRtl8139() {
    if (!info.rtl8139Present || !info.rxEnabled) return;
    uint16_t base = static_cast<uint16_t>(info.ioBase);
    for (int packets = 0; packets < 16; packets++) {
        uint8_t cr = inb(base + 0x37);
        if (cr & 0x01) break; // RX buffer empty

        uint16_t status = *reinterpret_cast<volatile uint16_t*>(rtlRxBuffer + rxOffset);
        uint16_t size = *reinterpret_cast<volatile uint16_t*>(rtlRxBuffer + rxOffset + 2);
        if (size < 4 || size > 1600) break;
        uint8_t* frame = rtlRxBuffer + rxOffset + 4;
        if (status & 0x0001) {
            uint16_t frameLen = size >= 4 ? static_cast<uint16_t>(size - 4) : size;
            info.rxPackets++;
            handleFrame(frame, frameLen);
        }
        rxOffset = static_cast<uint16_t>((rxOffset + size + 4 + 3) & ~3);
        rxOffset %= 8192;
        outw(base + 0x38, static_cast<uint16_t>(rxOffset - 16));
        outw(base + 0x3E, 0x0001);
    }
}

void Network::pollRtl8169() {
    if (!info.rtl8139Present || !info.rxEnabled) return;
    for (int packets = 0; packets < RTL8169_RX_DESC_COUNT; packets++) {
        Rtl8169Desc& desc = rtl8169RxDesc[rxOffset];
        uint32_t command = desc.command;
        if (command & RTL_DESC_OWN) break;

        uint16_t size = static_cast<uint16_t>(command & 0x3FFF);
        bool firstLast = (command & (RTL_DESC_FS | RTL_DESC_LS)) == (RTL_DESC_FS | RTL_DESC_LS);
        if (firstLast && size >= 4 && size <= RTL8169_RX_BUF_SIZE) {
            uint16_t frameLen = static_cast<uint16_t>(size - 4); // strip Ethernet FCS
            info.rxPackets++;
            handleFrame(rtl8169RxBuffers[rxOffset], frameLen);
        }

        uint32_t eor = (rxOffset == RTL8169_RX_DESC_COUNT - 1) ? RTL_DESC_EOR : 0;
        desc.vlan = 0;
        desc.lowBuf = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rtl8169RxBuffers[rxOffset]));
        desc.highBuf = 0;
        desc.command = RTL_DESC_OWN | eor | RTL8169_RX_BUF_SIZE;
        rxOffset = static_cast<uint16_t>((rxOffset + 1) % RTL8169_RX_DESC_COUNT);
        rtl8169Write16(0x3E, 0x0001); // acknowledge RxOK if pending
    }
}

void Network::handleFrame(const uint8_t* frame, uint16_t length) {
    if (length < 14) return;
    uint16_t type = rd16(frame + 12);
    if (type == ETH_ARP) handleArp(frame + 14, static_cast<uint16_t>(length - 14));
    else if (type == ETH_IPV4) handleIpv4(frame + 14, static_cast<uint16_t>(length - 14));
}

void Network::handleArp(const uint8_t* p, uint16_t length) {
    if (length < 28 || rd16(p + 0) != 1 || rd16(p + 2) != ETH_IPV4 || p[4] != 6 || p[5] != 4) return;
    uint16_t op = rd16(p + 6);
    uint32_t senderIp = rd32(p + 14);
    uint32_t targetIp = rd32(p + 24);
    rememberArp(senderIp, p + 8);
    info.arpPackets++;
    if (op == 2) Serial::writeString("[net] ARP reply received\n");

    // Reply to who-has our IP.
    if (op == 1 && targetIp == info.ipAddress) {
        uint8_t r[28];
        wr16(r + 0, 1); wr16(r + 2, ETH_IPV4); r[4] = 6; r[5] = 4; wr16(r + 6, 2);
        memcpy(r + 8, info.mac, 6); wr32(r + 14, info.ipAddress);
        memcpy(r + 18, p + 8, 6); wr32(r + 24, senderIp);
        sendFrame(p + 8, ETH_ARP, r, sizeof(r));
    }
}

void Network::handleIpv4(const uint8_t* p, uint16_t length) {
    if (length < 20 || (p[0] >> 4) != 4) return;
    uint16_t totalLen = rd16(p + 2); uint8_t ihl = static_cast<uint8_t>((p[0] & 0x0F) * 4);
    uint32_t srcIp = rd32(p + 12);
    uint32_t dstIp = rd32(p + 16);
    if (ihl < 20 || totalLen > length) return;
    bool dhcpClientPacket = false;
    if (p[9] == 17 && totalLen >= ihl + 8) {
        const uint8_t* udp = p + ihl;
        dhcpClientPacket = (rd16(udp + 0) == 67 && rd16(udp + 2) == 68);
    }
    if (dstIp != info.ipAddress && dstIp != BROADCAST_IP && !dhcpClientPacket) return;
    info.ipv4Packets++;
    if (p[9] == 1 && totalLen >= ihl + 8) {
        const uint8_t* icmp = p + ihl;
        if (icmp[0] == 0 && rd16(icmp + 6) == lastEchoSeq) {
            // Some NAT routers rewrite ICMP echo identifiers.  We only send one
            // echo at a time, so the sequence plus destination-to-us filter is a
            // safe match and avoids dropping valid public-internet replies.
            if (rd16(icmp + 4) != lastEchoId) {
                Serial::writeString("[net] ICMP echo reply id was rewritten\n");
            }
            echoReplySeen = true;
            lastEchoReplyIp = srcIp;
            lastEchoReplyTtl = p[8];
            info.icmpPackets++;
            Serial::writeString("[net] ICMP echo reply received\n");
        } else if (icmp[0] == 11 && totalLen >= ihl + 36) {
            const uint8_t* innerIp = icmp + 8;
            uint8_t innerIhl = static_cast<uint8_t>((innerIp[0] & 0x0F) * 4);
            if ((innerIp[0] >> 4) == 4 && innerIhl >= 20 && innerIp[9] == 1 && ihl + 8 + innerIhl + 8 <= totalLen) {
                const uint8_t* innerIcmp = innerIp + innerIhl;
                if (rd16(innerIcmp + 6) == lastIcmpErrorSeq) {
                    // Routers/NATs can rewrite the inner ICMP identifier in
                    // the quoted packet.  Sequence is enough here because this
                    // traceroute sends one probe at a time.
                    if (rd16(innerIcmp + 4) != lastEchoId) {
                        Serial::writeString("[net] ICMP time exceeded inner id was rewritten\n");
                    }
                    icmpTimeExceededSeen = true;
                    lastIcmpErrorIp = srcIp;
                    info.icmpPackets++;
                    Serial::writeString("[net] ICMP time exceeded received\n");
                }
            }
        }
    } else if (p[9] == 17) {
        handleUdp(p, ihl, totalLen);
    } else if (p[9] == 6) {
        handleTcp(p, ihl, totalLen);
    }
}

void Network::handleTcp(const uint8_t* ipPacket, uint8_t ihl, uint16_t totalLen) {
    if (totalLen < ihl + 20) return;
    uint32_t srcIp = rd32(ipPacket + 12);
    const uint8_t* tcp = ipPacket + ihl;
    uint16_t srcPort = rd16(tcp + 0);
    uint16_t dstPort = rd16(tcp + 2);
    uint32_t seq = rd32(tcp + 4);
    uint32_t ack = rd32(tcp + 8);
    uint8_t dataOffset = static_cast<uint8_t>((tcp[12] >> 4) * 4);
    uint8_t flags = tcp[13];
    if (srcIp != lastTcpRemoteIp || srcPort != lastTcpDestPort || dstPort != lastTcpSourcePort || dataOffset < 20) return;
    if (ihl + dataOffset > totalLen) return;
    if (flags & 0x04) { tcpRstSeen = true; Serial::writeString("[net] TCP RST received\n"); return; }
    if ((flags & 0x12) == 0x12 && ack == lastTcpSeq + 1) {
        lastTcpAck = seq + 1;
        lastTcpSeq = ack;
        tcpSynAckSeen = true;
        Serial::writeString("[net] TCP SYN-ACK received\n");
    }

    uint16_t payloadLen = static_cast<uint16_t>(totalLen - ihl - dataOffset);
    if (payloadLen > 0) {
        const uint8_t* payload = tcp + dataOffset;
        uint16_t copyLen = payloadLen;
        if (copyLen > sizeof(tcpRxBuffer) - 1 - tcpRxLen) copyLen = static_cast<uint16_t>(sizeof(tcpRxBuffer) - 1 - tcpRxLen);
        for (uint16_t i = 0; i < copyLen; i++) tcpRxBuffer[tcpRxLen++] = static_cast<char>(payload[i]);
        tcpRxBuffer[tcpRxLen] = 0;
        lastTcpAck = seq + payloadLen;
        tcpDataSeen = true;
        Serial::writeString("[net] TCP data received\n");
        sendTcpPacket(lastTcpRemoteIp, lastTcpSourcePort, lastTcpDestPort, lastTcpSeq, lastTcpAck, 0x10, 0, 0);
    }

    if (flags & 0x01) {
        lastTcpAck = seq + payloadLen + 1;
        tcpFinSeen = true;
        Serial::writeString("[net] TCP FIN received\n");
        sendTcpPacket(lastTcpRemoteIp, lastTcpSourcePort, lastTcpDestPort, lastTcpSeq, lastTcpAck, 0x10, 0, 0);
    }
}

void Network::handleUdp(const uint8_t* ipPacket, uint8_t ihl, uint16_t totalLen) {
    if (totalLen < ihl + 8) return;
    const uint8_t* udp = ipPacket + ihl;
    uint16_t sourcePort = rd16(udp + 0);
    uint16_t destPort = rd16(udp + 2);
    uint16_t udpLen = rd16(udp + 4);
    if (udpLen < 8 || ihl + udpLen > totalLen) return;

    if (sourcePort == 53 && destPort == 5300) {
        handleDnsResponse(udp + 8, static_cast<uint16_t>(udpLen - 8));
    } else if (sourcePort == 67 && destPort == 68) {
        handleDhcpResponse(udp + 8, static_cast<uint16_t>(udpLen - 8));
    }
}

void Network::handleDnsResponse(const uint8_t* data, uint16_t length) {
    if (length < 12) return;
    uint16_t id = rd16(data + 0);
    uint16_t flags = rd16(data + 2);
    uint16_t qd = rd16(data + 4);
    uint16_t an = rd16(data + 6);
    if (id != lastDnsId || (flags & 0x8000) == 0 || qd == 0 || an == 0) return;

    uint16_t off = 12;
    for (uint16_t q = 0; q < qd; q++) {
        while (off < length && data[off] != 0) {
            uint8_t labelLen = data[off++];
            if ((labelLen & 0xC0) != 0 || off + labelLen > length) return;
            off = static_cast<uint16_t>(off + labelLen);
        }
        if (off + 5 > length) return;
        off++;      // root label
        off += 4;   // qtype + qclass
    }

    for (uint16_t a = 0; a < an; a++) {
        if (off >= length) return;
        if ((data[off] & 0xC0) == 0xC0) {
            if (off + 2 > length) return;
            off += 2;
        } else {
            while (off < length && data[off] != 0) {
                uint8_t labelLen = data[off++];
                if ((labelLen & 0xC0) != 0 || off + labelLen > length) return;
                off = static_cast<uint16_t>(off + labelLen);
            }
            if (off >= length) return;
            off++;
        }
        if (off + 10 > length) return;
        uint16_t type = rd16(data + off); off += 2;
        uint16_t klass = rd16(data + off); off += 2;
        off += 4; // TTL
        uint16_t rdLen = rd16(data + off); off += 2;
        if (off + rdLen > length) return;
        if (type == 1 && klass == 1 && rdLen == 4) {
            lastDnsIp = rd32(data + off);
            dnsReplySeen = true;
            Serial::writeString("[net] DNS A reply received\n");
            return;
        }
        off = static_cast<uint16_t>(off + rdLen);
    }
}

bool Network::arping(uint32_t targetIp) {
    if (!info.rtl8139Present) return false;
    arpValid = false;
    sendArpRequest(targetIp);
    uint32_t deadline = timerMillis() + 2000;
    while (timerMillis() < deadline) {
        poll();
        if (arpValid && rd32(arpIp) == targetIp) return true;
    }
    return false;
}

bool Network::ping(uint32_t targetIp) {
    PingResult result;
    return pingOnce(targetIp, nextIcmpSeq++, 0, &result);
}

bool Network::pingOnce(uint32_t targetIp, uint16_t sequence, uint32_t startMs, PingResult* outResult) {
    if (outResult) { outResult->received = false; outResult->sequence = sequence; outResult->ttl = 0; outResult->elapsedMs = 0; outResult->fromIp = 0; }
    uint8_t mac[6];
    uint32_t nextHop = routeNextHop(targetIp);
    char targetText[16];
    char nextHopText[16];
    formatIp(targetIp, targetText, sizeof(targetText));
    formatIp(nextHop, nextHopText, sizeof(nextHopText));
    Serial::writeString("[net] route target ");
    Serial::writeString(targetText);
    Serial::writeString(" via ");
    Serial::writeString(nextHopText);
    Serial::writeString("\n");
    if (!lookupArp(nextHop, mac)) {
        if (!arping(nextHop)) return false;
        if (!lookupArp(nextHop, mac)) return false;
    }
    if (!sendIcmpEchoWithTtl(targetIp, mac, 64, sequence)) return false;
    uint32_t deadline = timerMillis() + 3000;
    while (timerMillis() < deadline) {
        poll();
        if (echoReplySeen) {
            if (outResult) {
                outResult->received = true;
                outResult->sequence = sequence;
                outResult->ttl = lastEchoReplyTtl ? lastEchoReplyTtl : 64;
                outResult->fromIp = lastEchoReplyIp;
                uint32_t now = timerMillis();
                outResult->elapsedMs = startMs ? (now - startMs) : 0;
            }
            return true;
        }
    }
    return false;
}

bool Network::tracerouteHop(uint32_t targetIp, uint8_t ttl, uint16_t sequence, uint32_t startMs, TraceHopResult* outResult) {
    if (outResult) { outResult->received = false; outResult->destinationReached = false; outResult->ttl = ttl; outResult->fromIp = 0; outResult->elapsedMs = 0; }
    uint8_t mac[6];
    uint32_t nextHop = routeNextHop(targetIp);
    char targetText[16];
    char nextHopText[16];
    formatIp(targetIp, targetText, sizeof(targetText));
    formatIp(nextHop, nextHopText, sizeof(nextHopText));
    Serial::writeString("[net] route target ");
    Serial::writeString(targetText);
    Serial::writeString(" via ");
    Serial::writeString(nextHopText);
    Serial::writeString(" ttl=");
    Serial::writeHex8(ttl);
    Serial::writeString("\n");
    if (!lookupArp(nextHop, mac)) {
        if (!arping(nextHop)) return false;
        if (!lookupArp(nextHop, mac)) return false;
    }
    if (!sendIcmpEchoWithTtl(targetIp, mac, ttl, sequence)) return false;
    uint32_t deadline = timerMillis() + 3000;
    while (timerMillis() < deadline) {
        poll();
        if (echoReplySeen || icmpTimeExceededSeen) {
            if (outResult) {
                outResult->received = true;
                outResult->destinationReached = echoReplySeen;
                outResult->ttl = ttl;
                outResult->fromIp = echoReplySeen ? lastEchoReplyIp : lastIcmpErrorIp;
                uint32_t now = timerMillis();
                outResult->elapsedMs = startMs ? (now - startMs) : 0;
            }
            return true;
        }
    }
    return false;
}

bool Network::sendUdpText(uint32_t targetIp, uint16_t destPort, const char* text) {
    if (!text || !info.rtl8139Present) return false;
    uint16_t textLen = 0;
    while (text[textLen] && textLen < 80) textLen++;
    bool ok = sendUdpPacket(targetIp, 9001, destPort, reinterpret_cast<const uint8_t*>(text), textLen);
    if (ok) Serial::writeString("[net] UDP text sent\n");
    return ok;
}

bool Network::sendUdpPacket(uint32_t targetIp, uint16_t sourcePort, uint16_t destPort, const uint8_t* data, uint16_t dataLen) {
    if (!data || dataLen > 512 || !info.rtl8139Present) return false;
    uint8_t mac[6];
    uint32_t nextHop = routeNextHop(targetIp);
    if (!lookupArp(nextHop, mac)) {
        if (!arping(nextHop)) return false;
        if (!lookupArp(nextHop, mac)) return false;
    }

    uint16_t udpLen = static_cast<uint16_t>(8 + dataLen);
    uint16_t ipLen = static_cast<uint16_t>(20 + udpLen);
    uint8_t p[20 + 8 + 512];
    memset(p, 0, sizeof(p));

    p[0] = 0x45;
    wr16(p + 2, ipLen);
    wr16(p + 4, nextIcmpSeq++);
    p[8] = 64;
    p[9] = 17; // UDP
    wr32(p + 12, info.ipAddress);
    wr32(p + 16, targetIp);
    wr16(p + 10, checksum(p, 20));

    wr16(p + 20, sourcePort);
    wr16(p + 22, destPort);  // destination port
    wr16(p + 24, udpLen);
    wr16(p + 26, 0);         // IPv4 UDP checksum optional
    memcpy(p + 28, data, dataLen);

    return sendFrame(mac, ETH_IPV4, p, ipLen);
}

bool Network::resolveDnsA(const char* hostname, uint32_t* outIp) {
    if (!hostname || !outIp || !info.rtl8139Present) return false;

    uint8_t query[256];
    memset(query, 0, sizeof(query));
    lastDnsId = static_cast<uint16_t>(0x4840 + (nextIcmpSeq++ & 0x00ff));
    dnsReplySeen = false;
    lastDnsIp = 0;

    wr16(query + 0, lastDnsId);
    wr16(query + 2, 0x0100); // standard recursive query
    wr16(query + 4, 1);      // one question

    uint16_t off = 12;
    int labelStart = 0;
    for (int i = 0;; i++) {
        char c = hostname[i];
        if (c == '.' || c == 0 || c == ' ') {
            int labelLen = i - labelStart;
            if (labelLen <= 0 || labelLen > 63 || static_cast<uint32_t>(off) + static_cast<uint32_t>(labelLen) + 1u >= sizeof(query)) return false;
            query[off++] = static_cast<uint8_t>(labelLen);
            for (int j = labelStart; j < i; j++) query[off++] = static_cast<uint8_t>(hostname[j]);
            labelStart = i + 1;
            if (c == 0 || c == ' ') break;
        }
    }
    if (static_cast<uint32_t>(off) + 5u > sizeof(query)) return false;
    query[off++] = 0;
    wr16(query + off, 1); off += 2; // A
    wr16(query + off, 1); off += 2; // IN

    if (!sendUdpPacket(info.dnsIp, 5300, 53, query, off)) return false;
    Serial::writeString("[net] DNS query sent\n");

    uint32_t deadline = timerMillis() + 5000;
    while (timerMillis() < deadline) {
        poll();
        if (dnsReplySeen) {
            *outIp = lastDnsIp;
            return true;
        }
    }
    return false;
}


bool Network::sendDhcpMessage(uint8_t messageType, uint32_t requestedIp, uint32_t serverIp) {
    if (!info.rtl8139Present) return false;
    uint8_t dhcp[300];
    memset(dhcp, 0, sizeof(dhcp));
    dhcp[0] = 1; // BOOTREQUEST
    dhcp[1] = 1; // Ethernet
    dhcp[2] = 6;
    dhcp[3] = 0;
    wr32(dhcp + 4, dhcpXid);
    wr16(dhcp + 10, 0x8000); // broadcast flag
    memcpy(dhcp + 28, info.mac, 6);
    dhcp[236] = 99; dhcp[237] = 130; dhcp[238] = 83; dhcp[239] = 99;
    uint16_t o = 240;
    dhcp[o++] = 53; dhcp[o++] = 1; dhcp[o++] = messageType;
    dhcp[o++] = 55; dhcp[o++] = 4; dhcp[o++] = 1; dhcp[o++] = 3; dhcp[o++] = 6; dhcp[o++] = 51;
    if (requestedIp) { dhcp[o++] = 50; dhcp[o++] = 4; wr32(dhcp + o, requestedIp); o += 4; }
    if (serverIp) { dhcp[o++] = 54; dhcp[o++] = 4; wr32(dhcp + o, serverIp); o += 4; }
    dhcp[o++] = 255;

    uint16_t udpLen = static_cast<uint16_t>(8 + o);
    uint16_t ipLen = static_cast<uint16_t>(20 + udpLen);
    uint8_t framePayload[20 + 8 + 300];
    memset(framePayload, 0, sizeof(framePayload));
    framePayload[0] = 0x45;
    wr16(framePayload + 2, ipLen);
    wr16(framePayload + 4, nextIcmpSeq++);
    framePayload[8] = 64;
    framePayload[9] = 17;
    wr32(framePayload + 12, ZERO_IP);
    wr32(framePayload + 16, BROADCAST_IP);
    wr16(framePayload + 10, checksum(framePayload, 20));
    wr16(framePayload + 20, 68);
    wr16(framePayload + 22, 67);
    wr16(framePayload + 24, udpLen);
    wr16(framePayload + 26, 0);
    memcpy(framePayload + 28, dhcp, o);
    bool ok = sendFrame(BROADCAST_MAC, ETH_IPV4, framePayload, ipLen);
    if (ok) Serial::writeString(messageType == 1 ? "[net] DHCPDISCOVER sent\n" : "[net] DHCPREQUEST sent\n");
    return ok;
}

void Network::handleDhcpResponse(const uint8_t* data, uint16_t length) {
    if (length < 240 || data[0] != 2 || data[1] != 1 || data[2] != 6) return;
    if (rd32(data + 4) != dhcpXid) return;
    for (int m = 0; m < 6; m++) { if (data[28 + m] != info.mac[m]) return; }
    if (data[236] != 99 || data[237] != 130 || data[238] != 83 || data[239] != 99) return;

    uint32_t yiaddr = rd32(data + 16);
    uint8_t msgType = 0;
    uint32_t router = 0;
    uint32_t dns = 0;
    uint32_t mask = 0;
    uint32_t server = 0;
    uint16_t off = 240;
    while (off < length) {
        uint8_t opt = data[off++];
        if (opt == 0) continue;
        if (opt == 255) break;
        if (off >= length) break;
        uint8_t len = data[off++];
        if (off + len > length) break;
        if (opt == 53 && len >= 1) msgType = data[off];
        else if (opt == 1 && len >= 4) mask = rd32(data + off);
        else if (opt == 3 && len >= 4) router = rd32(data + off);
        else if (opt == 6 && len >= 4) dns = rd32(data + off);
        else if (opt == 54 && len >= 4) server = rd32(data + off);
        off = static_cast<uint16_t>(off + len);
    }

    if (msgType == 2) {
        dhcpOfferedIp = yiaddr;
        dhcpServerIp = server;
        dhcpRouterIp = router;
        dhcpDnsIp = dns;
        dhcpNetmask = mask;
        dhcpOfferSeen = true;
        Serial::writeString("[net] DHCPOFFER received\n");
    } else if (msgType == 5) {
        info.ipAddress = yiaddr;
        if (router) info.gatewayIp = router;
        else if (dhcpRouterIp) info.gatewayIp = dhcpRouterIp;
        if (dns) info.dnsIp = dns;
        else if (dhcpDnsIp) info.dnsIp = dhcpDnsIp;
        info.netmask = mask ? mask : (dhcpNetmask ? dhcpNetmask : defaultNetmaskForIp(yiaddr));
        info.dhcpConfigured = true;
        dhcpAckSeen = true;
        arpValid = false;
        Serial::writeString("[net] DHCPACK received\n");
    }
}

bool Network::startDhcp() {
    if (!info.rtl8139Present) return false;
    dhcpOfferSeen = false;
    dhcpAckSeen = false;
    dhcpOfferedIp = 0;
    dhcpServerIp = 0;
    dhcpRouterIp = 0;
    dhcpDnsIp = 0;
    dhcpNetmask = 0;
    dhcpXid = 0x484B0000u + nextIcmpSeq++;
    arpValid = false;
    info.dhcpConfigured = false;
    info.ipAddress = 0;
    info.gatewayIp = 0;
    info.dnsIp = 0;
    info.netmask = 0;

    if (!sendDhcpMessage(1, 0, 0)) {
        dhcpState = 4;
        return false;
    }
    dhcpState = 1;
    dhcpStateStartMs = timerMillis();
    return true;
}

void Network::tickDhcp() {
    if (dhcpState == 0 || dhcpState == 3 || dhcpState == 4) return;
    poll();
    uint32_t now = timerMillis();
    if (dhcpState == 1) {
        if (dhcpOfferSeen && dhcpOfferedIp != 0) {
            if (sendDhcpMessage(3, dhcpOfferedIp, dhcpServerIp)) {
                dhcpState = 2;
                dhcpStateStartMs = now;
            } else {
                dhcpState = 4;
            }
        } else if (now - dhcpStateStartMs >= 5000u) {
            dhcpState = 4;
        }
    } else if (dhcpState == 2) {
        if (dhcpAckSeen) {
            dhcpState = 3;
        } else if (now - dhcpStateStartMs >= 5000u) {
            dhcpState = 4;
        }
    }
}

uint8_t Network::getDhcpState() const { return dhcpState; }

bool Network::dhcpDiscover() {
    if (!startDhcp()) return false;
    while (dhcpState == 1 || dhcpState == 2) {
        tickDhcp();
        pollKeyboard();
        asm volatile("pause");
    }
    return dhcpState == 3;
}

bool Network::sendTcpPacket(uint32_t targetIp, uint16_t sourcePort, uint16_t destPort, uint32_t seq, uint32_t ack, uint8_t flags, const uint8_t* data, uint16_t dataLen) {
    if (dataLen > 256 || (!data && dataLen != 0) || !info.rtl8139Present) return false;
    uint8_t mac[6];
    uint32_t nextHop = routeNextHop(targetIp);
    if (!lookupArp(nextHop, mac)) {
        if (!arping(nextHop)) return false;
        if (!lookupArp(nextHop, mac)) return false;
    }

    uint16_t tcpLen = static_cast<uint16_t>(20 + dataLen);
    uint16_t ipLen = static_cast<uint16_t>(20 + tcpLen);
    uint8_t p[20 + 20 + 256];
    memset(p, 0, sizeof(p));

    p[0] = 0x45;
    wr16(p + 2, ipLen);
    wr16(p + 4, nextIcmpSeq++);
    p[8] = 64;
    p[9] = 6; // TCP
    wr32(p + 12, info.ipAddress);
    wr32(p + 16, targetIp);
    wr16(p + 10, checksum(p, 20));

    uint8_t* tcp = p + 20;
    wr16(tcp + 0, sourcePort);
    wr16(tcp + 2, destPort);
    wr32(tcp + 4, seq);
    wr32(tcp + 8, ack);
    tcp[12] = 5 << 4;
    tcp[13] = flags;
    wr16(tcp + 14, 4096); // small receive window
    wr16(tcp + 16, 0);
    wr16(tcp + 18, 0);
    if (dataLen) memcpy(tcp + 20, data, dataLen);
    wr16(tcp + 16, tcpChecksum(info.ipAddress, targetIp, tcp, tcpLen));

    return sendFrame(mac, ETH_IPV4, p, ipLen);
}

bool Network::sendTcpText(uint32_t targetIp, uint16_t destPort, const char* text) {
    if (!text || !info.rtl8139Present) return false;
    uint16_t textLen = 0;
    while (text[textLen] && textLen < 120) textLen++;

    lastTcpRemoteIp = targetIp;
    lastTcpDestPort = destPort;
    lastTcpSourcePort = static_cast<uint16_t>(40000 + (nextIcmpSeq & 0x0fff));
    lastTcpSeq = 0x484B0000u + nextIcmpSeq++;
    lastTcpAck = 0;
    tcpSynAckSeen = false;
    tcpFinSeen = false;
    tcpRstSeen = false;
    tcpDataSeen = false;
    tcpRxLen = 0;
    tcpRxBuffer[0] = 0;

    if (!sendTcpPacket(targetIp, lastTcpSourcePort, destPort, lastTcpSeq, 0, 0x02, 0, 0)) return false;
    Serial::writeString("[net] TCP SYN sent\n");
    uint32_t synDeadline = timerMillis() + 5000;
    while (timerMillis() < synDeadline) {
        poll();
        if (tcpRstSeen) return false;
        if (tcpSynAckSeen) break;
    }
    if (!tcpSynAckSeen) return false;

    if (!sendTcpPacket(targetIp, lastTcpSourcePort, destPort, lastTcpSeq, lastTcpAck, 0x10, 0, 0)) return false;
    Serial::writeString("[net] TCP ACK sent\n");
    if (textLen) {
        if (!sendTcpPacket(targetIp, lastTcpSourcePort, destPort, lastTcpSeq, lastTcpAck, 0x18, reinterpret_cast<const uint8_t*>(text), textLen)) return false;
        lastTcpSeq += textLen;
        Serial::writeString("[net] TCP text sent\n");
    }
    sendTcpPacket(targetIp, lastTcpSourcePort, destPort, lastTcpSeq, lastTcpAck, 0x11, 0, 0);
    Serial::writeString("[net] TCP FIN sent\n");
    return true;
}

bool Network::tcpRequestRaw(uint32_t targetIp, uint16_t destPort, const uint8_t* data, uint16_t dataLen, uint8_t* outResponse, uint16_t outCap, uint16_t* outLen) {
    if ((!data && dataLen != 0) || dataLen > 1024 || !info.rtl8139Present) return false;
    if (outLen) *outLen = 0;

    lastTcpRemoteIp = targetIp;
    lastTcpDestPort = destPort;
    lastTcpSourcePort = static_cast<uint16_t>(42000 + (nextIcmpSeq & 0x0fff));
    lastTcpSeq = 0x48500000u + nextIcmpSeq++;
    lastTcpAck = 0;
    tcpSynAckSeen = false;
    tcpFinSeen = false;
    tcpRstSeen = false;
    tcpDataSeen = false;
    tcpRxLen = 0;
    tcpRxBuffer[0] = 0;

    if (!sendTcpPacket(targetIp, lastTcpSourcePort, destPort, lastTcpSeq, 0, 0x02, 0, 0)) return false;
    Serial::writeString("[net] TCP SYN sent\n");
    uint32_t synDeadline = timerMillis() + 5000;
    while (timerMillis() < synDeadline) {
        poll();
        if (tcpRstSeen) return false;
        if (tcpSynAckSeen) break;
    }
    if (!tcpSynAckSeen) return false;

    if (!sendTcpPacket(targetIp, lastTcpSourcePort, destPort, lastTcpSeq, lastTcpAck, 0x10, 0, 0)) return false;
    Serial::writeString("[net] TCP ACK sent\n");
    if (dataLen) {
        uint16_t off = 0;
        while (off < dataLen) {
            uint16_t chunk = static_cast<uint16_t>(dataLen - off);
            if (chunk > 256) chunk = 256;
            if (!sendTcpPacket(targetIp, lastTcpSourcePort, destPort, lastTcpSeq, lastTcpAck, 0x18, data + off, chunk)) return false;
            lastTcpSeq += chunk;
            off = static_cast<uint16_t>(off + chunk);
            poll();
        }
        Serial::writeString("[net] TCP data sent\n");
    }

    uint32_t dataDeadline = timerMillis() + 8000;
    while (timerMillis() < dataDeadline) {
        poll();
        if (tcpRstSeen) break;
        if (tcpFinSeen) break;
        if (tcpDataSeen && tcpRxLen >= sizeof(tcpRxBuffer) - 1) break;
    }

    if (outResponse && outCap > 0) {
        uint16_t copyLen = tcpRxLen;
        if (copyLen > outCap) copyLen = outCap;
        for (uint16_t i = 0; i < copyLen; i++) outResponse[i] = static_cast<uint8_t>(tcpRxBuffer[i]);
        if (outLen) *outLen = copyLen;
    } else if (outLen) {
        *outLen = tcpRxLen;
    }
    if (!tcpFinSeen) {
        sendTcpPacket(targetIp, lastTcpSourcePort, destPort, lastTcpSeq, lastTcpAck, 0x11, 0, 0);
        Serial::writeString("[net] TCP FIN sent\n");
    }
    return tcpSynAckSeen && !tcpRstSeen;
}

bool Network::tcpRequestText(uint32_t targetIp, uint16_t destPort, const char* text, char* outResponse, uint16_t outLen) {
    if (!text || !info.rtl8139Present) return false;
    uint16_t textLen = 0;
    while (text[textLen] && textLen < 1024) textLen++;
    uint16_t rawLen = 0;
    bool ok = tcpRequestRaw(targetIp, destPort, reinterpret_cast<const uint8_t*>(text), textLen, reinterpret_cast<uint8_t*>(outResponse), outLen > 0 ? static_cast<uint16_t>(outLen - 1) : 0, &rawLen);
    if (outResponse && outLen > 0) {
        uint16_t nul = rawLen;
        if (nul > outLen - 1) nul = static_cast<uint16_t>(outLen - 1);
        outResponse[nul] = 0;
    }
    return ok;
}

// --- Persistent TCP session helpers (used by the SOCKS5 client) ---------------
// tcpRequestRaw is one-shot (SYN..send..drain..FIN). SOCKS5 negotiation needs
// several request/response exchanges over one connection, so these keep the
// connection open and track consumption of tcpRxBuffer via tcpConsumed.

bool Network::tcpSessionOpen(uint32_t targetIp, uint16_t destPort) {
    if (!info.rtl8139Present) return false;

    lastTcpRemoteIp = targetIp;
    lastTcpDestPort = destPort;
    lastTcpSourcePort = static_cast<uint16_t>(43000 + (nextIcmpSeq & 0x0fff));
    lastTcpSeq = 0x53430000u + nextIcmpSeq++; // 'SC' session base
    lastTcpAck = 0;
    tcpSynAckSeen = false;
    tcpFinSeen = false;
    tcpRstSeen = false;
    tcpDataSeen = false;
    tcpRxLen = 0;
    tcpConsumed = 0;
    tcpRxBuffer[0] = 0;

    if (!sendTcpPacket(targetIp, lastTcpSourcePort, destPort, lastTcpSeq, 0, 0x02, 0, 0)) return false;
    Serial::writeString("[net] TCP session SYN sent\n");
    uint32_t deadline = timerMillis() + 5000;
    while (timerMillis() < deadline) {
        poll();
        if (tcpRstSeen) return false;
        if (tcpSynAckSeen) break;
    }
    if (!tcpSynAckSeen) return false;

    // Complete the handshake; the connection then stays open for exchanges.
    return sendTcpPacket(lastTcpRemoteIp, lastTcpSourcePort, lastTcpDestPort, lastTcpSeq, lastTcpAck, 0x10, 0, 0);
}

bool Network::tcpSessionSend(const uint8_t* data, uint16_t len) {
    if (!info.rtl8139Present || !tcpSynAckSeen || tcpRstSeen) return false;
    if (len == 0) return true;
    if (!data) return false;
    uint16_t off = 0;
    while (off < len) {
        uint16_t chunk = static_cast<uint16_t>(len - off);
        if (chunk > 256) chunk = 256;
        if (!sendTcpPacket(lastTcpRemoteIp, lastTcpSourcePort, lastTcpDestPort, lastTcpSeq, lastTcpAck, 0x18, data + off, chunk)) return false;
        lastTcpSeq += chunk;
        off = static_cast<uint16_t>(off + chunk);
        poll(); // let cumulative ACKs / an early reply drain between segments
    }
    return true;
}

bool Network::tcpSessionWait(uint16_t needed, uint32_t totalMs) {
    uint32_t deadline = timerMillis() + totalMs;
    while (timerMillis() < deadline) {
        poll();
        if (tcpRstSeen) return false;
        if (static_cast<uint16_t>(tcpRxLen - tcpConsumed) >= needed) return true;
        if (tcpFinSeen) break;
    }
    return static_cast<uint16_t>(tcpRxLen - tcpConsumed) >= needed;
}

uint16_t Network::tcpSessionDrain(uint8_t* out, uint16_t cap, uint32_t quietMs, uint32_t totalMs) {
    uint32_t start = timerMillis();
    uint16_t lastSeen = tcpRxLen;
    uint32_t lastChange = start;
    for (;;) {
        poll();
        uint32_t now = timerMillis();
        if (tcpRxLen != lastSeen) { lastSeen = tcpRxLen; lastChange = now; }
        if (tcpRstSeen || tcpFinSeen) break;
        if (static_cast<uint16_t>(tcpRxLen - tcpConsumed) > 0 && (now - lastChange) >= quietMs) break;
        if (now - start >= totalMs) break;
    }
    uint16_t avail = static_cast<uint16_t>(tcpRxLen - tcpConsumed);
    uint16_t copyLen = avail < cap ? avail : cap;
    if (out) {
        for (uint16_t i = 0; i < copyLen; i++) out[i] = static_cast<uint8_t>(tcpRxBuffer[tcpConsumed + i]);
    }
    tcpConsumed = static_cast<uint16_t>(tcpConsumed + copyLen);
    return copyLen;
}

void Network::tcpSessionClose() {
    if (info.rtl8139Present && tcpSynAckSeen && !tcpRstSeen && !tcpFinSeen) {
        sendTcpPacket(lastTcpRemoteIp, lastTcpSourcePort, lastTcpDestPort, lastTcpSeq, lastTcpAck, 0x11, 0, 0);
        Serial::writeString("[net] TCP session FIN sent\n");
    }
}

bool Network::tcpStreamOpen(uint32_t targetIp, uint16_t destPort) {
    return tcpSessionOpen(targetIp, destPort);
}

bool Network::tcpStreamSend(const uint8_t* data, uint16_t len) {
    return tcpSessionSend(data, len);
}

bool Network::tcpStreamWait(uint16_t needed, uint32_t totalMs) {
    return tcpSessionWait(needed, totalMs);
}

uint16_t Network::tcpStreamDrain(uint8_t* out, uint16_t cap, uint32_t quietMs, uint32_t totalMs) {
    return tcpSessionDrain(out, cap, quietMs, totalMs);
}

void Network::tcpStreamClose() {
    tcpSessionClose();
}

bool Network::socks5Connect(uint32_t proxyIp, uint16_t proxyPort,
                            const char* destHost, uint32_t destIp, uint16_t destPort,
                            const char* username, const char* password,
                            const uint8_t* appData, uint16_t appLen,
                            uint8_t* outResponse, uint16_t outCap, uint16_t* outLen,
                            Socks5Result* outResult) {
    Socks5Result local;
    local.tcpConnected = false;
    local.methodSelected = false;
    local.method = 0xFF;
    local.authPerformed = false;
    local.authOk = false;
    local.connectOk = false;
    local.replyCode = 0xFF;
    local.boundAtyp = 0;
    local.boundIp = 0;
    local.boundPort = 0;
    local.appResponseLen = 0;
    local.failPhase = 1; // TCP-to-proxy
    if (outLen) *outLen = 0;

    if (!info.rtl8139Present) { if (outResult) *outResult = local; return false; }

    // Prefer a domain target (ATYP 0x03) so the proxy resolves it remotely and
    // no local DNS query leaks the destination. Fall back to a literal IPv4.
    bool useDomain = (destHost && destHost[0] != '\0');
    uint16_t domLen = 0;
    if (useDomain) {
        while (destHost[domLen] && domLen < 255) domLen++;
        // Keep the whole CONNECT request inside one <=256-byte TCP segment.
        if (domLen == 0 || domLen > 248) { if (outResult) *outResult = local; return false; }
    }

    // 1. TCP to the proxy.
    if (!tcpSessionOpen(proxyIp, proxyPort)) { if (outResult) *outResult = local; return false; }
    local.tcpConnected = true;
    Serial::writeString("[net] SOCKS5 proxy TCP connected\n");

    // 2. Greeting: VER, NMETHODS, METHODS. Offer user/pass only with credentials.
    bool offerUserPass = (username && username[0] != '\0');
    uint8_t greet[4];
    uint8_t g = 0;
    greet[g++] = 0x05;
    greet[g++] = static_cast<uint8_t>(offerUserPass ? 2 : 1);
    greet[g++] = 0x00;                    // no authentication required
    if (offerUserPass) greet[g++] = 0x02; // username/password (RFC 1929)
    local.failPhase = 2;
    if (!tcpSessionSend(greet, g) || !tcpSessionWait(2, 6000)) { tcpSessionClose(); if (outResult) *outResult = local; return false; }
    const uint8_t* msel = reinterpret_cast<const uint8_t*>(tcpRxBuffer) + tcpConsumed;
    uint8_t selVer = msel[0];
    uint8_t selMethod = msel[1];
    tcpConsumed = static_cast<uint16_t>(tcpConsumed + 2);
    if (selVer != 0x05) { tcpSessionClose(); if (outResult) *outResult = local; return false; }
    local.methodSelected = true;
    local.method = selMethod;

    // 3. Authenticate if the proxy selected username/password.
    if (selMethod == 0xFF) { local.failPhase = 3; tcpSessionClose(); if (outResult) *outResult = local; return false; }
    if (selMethod == 0x02) {
        if (!offerUserPass) { local.failPhase = 4; tcpSessionClose(); if (outResult) *outResult = local; return false; }
        uint16_t ulen = 0; while (username[ulen] && ulen < 255) ulen++;
        uint16_t plen = 0; if (password) { while (password[plen] && plen < 255) plen++; }
        static uint8_t authMsg[1 + 1 + 255 + 1 + 255];
        uint16_t a = 0;
        authMsg[a++] = 0x01; // user/pass auth version
        authMsg[a++] = static_cast<uint8_t>(ulen);
        for (uint16_t i = 0; i < ulen; i++) authMsg[a++] = static_cast<uint8_t>(username[i]);
        authMsg[a++] = static_cast<uint8_t>(plen);
        for (uint16_t i = 0; i < plen; i++) authMsg[a++] = static_cast<uint8_t>(password[i]);
        local.failPhase = 5;
        local.authPerformed = true;
        if (!tcpSessionSend(authMsg, a) || !tcpSessionWait(2, 6000)) { tcpSessionClose(); if (outResult) *outResult = local; return false; }
        const uint8_t* areply = reinterpret_cast<const uint8_t*>(tcpRxBuffer) + tcpConsumed;
        uint8_t authStatus = areply[1];
        tcpConsumed = static_cast<uint16_t>(tcpConsumed + 2);
        if (authStatus != 0x00) { local.failPhase = 6; tcpSessionClose(); if (outResult) *outResult = local; return false; }
        local.authOk = true;
    } else if (selMethod == 0x00) {
        local.authOk = true;
    } else {
        local.failPhase = 3; tcpSessionClose(); if (outResult) *outResult = local; return false;
    }

    // 4. CONNECT request: VER CMD RSV ATYP DST.ADDR DST.PORT.
    static uint8_t connReq[262];
    uint16_t c = 0;
    connReq[c++] = 0x05; // VER
    connReq[c++] = 0x01; // CMD = CONNECT
    connReq[c++] = 0x00; // RSV
    if (useDomain) {
        connReq[c++] = 0x03;
        connReq[c++] = static_cast<uint8_t>(domLen);
        for (uint16_t i = 0; i < domLen; i++) connReq[c++] = static_cast<uint8_t>(destHost[i]);
    } else {
        connReq[c++] = 0x01;
        wr32(connReq + c, destIp); c = static_cast<uint16_t>(c + 4);
    }
    wr16(connReq + c, destPort); c = static_cast<uint16_t>(c + 2);
    local.failPhase = 7;
    if (!tcpSessionSend(connReq, c)) { tcpSessionClose(); if (outResult) *outResult = local; return false; }
    Serial::writeString("[net] SOCKS5 CONNECT sent\n");

    // 5. CONNECT reply. Length depends on the bound-address type.
    local.failPhase = 8;
    if (!tcpSessionWait(4, 8000)) { tcpSessionClose(); if (outResult) *outResult = local; return false; }
    const uint8_t* rep = reinterpret_cast<const uint8_t*>(tcpRxBuffer) + tcpConsumed;
    uint8_t atyp = rep[3];
    uint16_t replyTotal = 0;
    if (atyp == 0x01) {
        replyTotal = 4 + 4 + 2;          // IPv4
    } else if (atyp == 0x04) {
        replyTotal = 4 + 16 + 2;         // IPv6
    } else if (atyp == 0x03) {           // domain: byte 4 holds the length
        if (!tcpSessionWait(5, 4000)) { tcpSessionClose(); if (outResult) *outResult = local; return false; }
        rep = reinterpret_cast<const uint8_t*>(tcpRxBuffer) + tcpConsumed;
        replyTotal = static_cast<uint16_t>(4 + 1 + rep[4] + 2);
    } else {
        tcpSessionClose(); if (outResult) *outResult = local; return false;
    }
    if (!tcpSessionWait(replyTotal, 4000)) { tcpSessionClose(); if (outResult) *outResult = local; return false; }
    rep = reinterpret_cast<const uint8_t*>(tcpRxBuffer) + tcpConsumed;
    local.replyCode = rep[1];
    local.boundAtyp = atyp;
    if (atyp == 0x01) {
        local.boundIp = rd32(rep + 4);
        local.boundPort = rd16(rep + 8);
    } else if (atyp == 0x03) {
        local.boundPort = rd16(rep + 5 + rep[4]);
    } else if (atyp == 0x04) {
        local.boundPort = rd16(rep + 20);
    }
    tcpConsumed = static_cast<uint16_t>(tcpConsumed + replyTotal);
    if (local.replyCode != 0x00) { local.failPhase = 9; tcpSessionClose(); if (outResult) *outResult = local; return false; }
    local.connectOk = true;
    local.failPhase = 0;
    Serial::writeString("[net] SOCKS5 tunnel established\n");

    // 6. Optional application exchange over the open tunnel.
    if (appData && appLen > 0) {
        if (tcpSessionSend(appData, appLen)) {
            uint16_t got = tcpSessionDrain(outResponse, outCap, 600, 8000);
            local.appResponseLen = got;
            if (outLen) *outLen = got;
        }
    }

    tcpSessionClose();
    if (outResult) *outResult = local;
    return local.connectOk;
}
