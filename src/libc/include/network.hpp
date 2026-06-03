#ifndef NETWORK_HPP
#define NETWORK_HPP

#include <stdint.h>
#include <stddef.h>
#include <pci.hpp>

struct NetworkInfo {
    bool pciPresent;
    bool rtl8139Present;
    bool linkUp;
    bool rxEnabled;
    char nicName[32];
    uint8_t mac[6];
    uint32_t ioBase;
    uint8_t irqLine;
    uint32_t ipAddress;
    uint32_t gatewayIp;
    uint32_t dnsIp;
    uint32_t netmask;
    uint32_t rxPackets;
    uint32_t txPackets;
    uint32_t arpPackets;
    uint32_t ipv4Packets;
    uint32_t icmpPackets;
    bool dhcpConfigured;
};

struct PingResult {
    bool received;
    uint16_t sequence;
    uint8_t ttl;
    uint32_t elapsedMs;
    uint32_t fromIp;
};

struct TraceHopResult {
    bool received;
    bool destinationReached;
    uint8_t ttl;
    uint32_t fromIp;
    uint32_t elapsedMs;
};

class Network {
public:
    Network();
    void init();
    const NetworkInfo& getInfo() const;
    void formatMac(char* out, int outLen) const;
    void formatIp(uint32_t ip, char* out, int outLen) const;
    bool parseIp(const char* text, uint32_t* outIp) const;
    void poll();
    bool arping(uint32_t targetIp);
    bool ping(uint32_t targetIp);
    bool pingOnce(uint32_t targetIp, uint16_t sequence, uint32_t startMs, PingResult* outResult);
    bool tracerouteHop(uint32_t targetIp, uint8_t ttl, uint16_t sequence, uint32_t startMs, TraceHopResult* outResult);
    bool dhcpDiscover();
    bool startDhcp();
    void tickDhcp();
    uint8_t getDhcpState() const;
    bool sendUdpText(uint32_t targetIp, uint16_t destPort, const char* text);
    bool resolveDnsA(const char* hostname, uint32_t* outIp);
    bool sendTcpText(uint32_t targetIp, uint16_t destPort, const char* text);
    bool tcpRequestText(uint32_t targetIp, uint16_t destPort, const char* text, char* outResponse, uint16_t outLen);

private:
    NetworkInfo info;
    PciDeviceInfo pciDevice;
    uint32_t rxBufferPhys;
    uint16_t rxOffset;
    uint8_t txIndex;
    uint8_t arpIp[4];
    uint8_t arpMac[6];
    bool arpValid;
    uint16_t nextIcmpSeq;
    uint16_t lastEchoId;
    uint16_t lastEchoSeq;
    uint32_t lastEchoReplyIp;
    uint8_t lastEchoReplyTtl;
    bool echoReplySeen;
    bool icmpTimeExceededSeen;
    uint32_t lastIcmpErrorIp;
    uint16_t lastIcmpErrorSeq;
    bool dnsReplySeen;
    uint16_t lastDnsId;
    uint32_t lastDnsIp;
    uint16_t lastTcpSourcePort;
    uint16_t lastTcpDestPort;
    uint32_t lastTcpRemoteIp;
    uint32_t lastTcpSeq;
    uint32_t lastTcpAck;
    bool tcpSynAckSeen;
    bool tcpFinSeen;
    bool tcpRstSeen;
    bool tcpDataSeen;
    uint16_t tcpRxLen;
    char tcpRxBuffer[512];
    bool dhcpOfferSeen;
    bool dhcpAckSeen;
    uint32_t dhcpXid;
    uint32_t dhcpOfferedIp;
    uint32_t dhcpServerIp;
    uint32_t dhcpRouterIp;
    uint32_t dhcpDnsIp;
    uint32_t dhcpNetmask;
    uint8_t dhcpState;
    uint32_t dhcpStateStartMs;
    uint8_t nicKind;

    void clearInfo();
    void initRtl8139(const PciDeviceInfo& device);
    void initRtl8169(const PciDeviceInfo& device);
    bool sendFrame(const uint8_t* destMac, uint16_t etherType, const uint8_t* payload, uint16_t payloadLen);
    bool sendFrameRtl8139(const uint8_t* destMac, uint16_t etherType, const uint8_t* payload, uint16_t payloadLen);
    bool sendFrameRtl8169(const uint8_t* destMac, uint16_t etherType, const uint8_t* payload, uint16_t payloadLen);
    bool sendUdpPacket(uint32_t targetIp, uint16_t sourcePort, uint16_t destPort, const uint8_t* data, uint16_t dataLen);
    bool sendTcpPacket(uint32_t targetIp, uint16_t sourcePort, uint16_t destPort, uint32_t seq, uint32_t ack, uint8_t flags, const uint8_t* data, uint16_t dataLen);
    void handleFrame(const uint8_t* frame, uint16_t length);
    void pollRtl8139();
    void pollRtl8169();
    void handleArp(const uint8_t* payload, uint16_t length);
    void handleIpv4(const uint8_t* payload, uint16_t length);
    void handleUdp(const uint8_t* ipPacket, uint8_t ihl, uint16_t totalLen);
    void handleTcp(const uint8_t* ipPacket, uint8_t ihl, uint16_t totalLen);
    void handleDnsResponse(const uint8_t* data, uint16_t length);
    void handleDhcpResponse(const uint8_t* data, uint16_t length);
    bool sendDhcpMessage(uint8_t messageType, uint32_t requestedIp, uint32_t serverIp);
    void sendArpRequest(uint32_t targetIp);
    bool sendIcmpEcho(uint32_t targetIp, const uint8_t* destMac);
    bool sendIcmpEchoWithTtl(uint32_t targetIp, const uint8_t* destMac, uint8_t ttl, uint16_t sequence);
    bool lookupArp(uint32_t ip, uint8_t* outMac) const;
    void rememberArp(uint32_t ip, const uint8_t* mac);
    uint32_t routeNextHop(uint32_t targetIp) const;
};

#endif // NETWORK_HPP
