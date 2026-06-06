#ifndef TLS_HPP
#define TLS_HPP

#include <stdint.h>
#include <network.hpp>

// Minimal TLS 1.3 client (RFC 8446) speaking exactly one cipher suite:
//   TLS_AES_128_GCM_SHA256 (0x1301) over the x25519 group.
//
// Modern Tor relays require TLS 1.3 for the link handshake (they reject TLS
// 1.2), so this is the transport under Tor's link protocol. It does NOT
// validate the server certificate: Tor uses TLS only for confidentiality and
// authenticates the relay cryptographically inside the protocol (CERTS cells +
// the ntor handshake bound to the relay identity). The TCP stream must already
// be open on the Network object before handshake() is called.
class TlsClient {
public:
    void reset(Network* net);
    bool handshake(uint32_t timeoutMs);

    // Application-data I/O once established(). write() emits one TLS record;
    // read() returns up to `needed` decrypted bytes (== needed on success).
    bool write(const uint8_t* data, uint32_t len);
    uint32_t read(uint8_t* out, uint32_t needed, uint32_t timeoutMs);

    bool established() const { return est; }

    // Diagnostics for the `tor tls` command.
    const char* stageName() const { return stage; }
    bool sawServerHello() const { return gotServerHello; }
    bool sawCertificate() const { return gotCertificate; }
    bool sawServerKeyExchange() const { return gotKeyShare; }   // 1.3: server key_share
    bool sawServerHelloDone() const { return gotEncryptedExtensions; }
    bool sawServerFinished() const { return gotServerFinished; }
    uint16_t negotiatedSuite() const { return suite; }
    uint16_t negotiatedGroup() const { return group; }
    uint8_t alertLevel() const { return alertLvl; }
    uint8_t alertDescription() const { return alertDesc; }
    uint32_t certificateBytes() const { return certBytes; }

private:
    Network* net = nullptr;
    bool est = false;
    const char* stage = "idle";

    // Key-exchange material
    uint8_t clientRandom[32];
    uint8_t sessionId[32];      // legacy_session_id (middlebox compat); echoed back
    uint8_t clientPriv[32];
    uint8_t clientPub[32];
    uint8_t serverPub[32];      // server key_share

    // TLS 1.3 key schedule secrets
    uint8_t handshakeSecret[32];
    uint8_t masterSecret[32];
    uint8_t cHsTraffic[32];
    uint8_t sHsTraffic[32];
    uint8_t cApTraffic[32];
    uint8_t sApTraffic[32];

    // Active record-protection state (switched handshake -> application).
    uint8_t sendKey[16];
    uint8_t sendIV[12];
    uint8_t recvKey[16];
    uint8_t recvIV[12];
    uint64_t sendSeq = 0;
    uint64_t recvSeq = 0;
    bool haveSendKeys = false;
    bool haveRecvKeys = false;

    // Decrypted application-data byte buffer (read() hands bytes out of it).
    uint32_t appPos = 0;
    uint32_t appLen = 0;

    // Observed handshake facts
    uint16_t suite = 0;
    uint16_t group = 0;
    bool gotServerHello = false;
    bool gotKeyShare = false;
    bool gotEncryptedExtensions = false;
    bool gotCertificate = false;
    bool gotServerFinished = false;
    uint32_t certBytes = 0;
    uint8_t alertLvl = 0;
    uint8_t alertDesc = 0;

    // Record layer
    bool sendPlainRecord(uint8_t type, const uint8_t* data, uint32_t len);
    bool sendEncryptedRecord(uint8_t innerType, const uint8_t* data, uint32_t len);
    bool readRecord(uint8_t* typeOut, uint8_t* body, uint32_t cap, uint32_t* lenOut, uint32_t timeoutMs);
    int32_t decryptRecord(const uint8_t* body, uint32_t bodyLen, uint8_t* out, uint8_t* innerTypeOut);

    // Handshake steps
    bool sendClientHello();
    bool readServerHello(uint32_t timeoutMs);
    void deriveHandshakeKeys();
    bool readEncryptedFlight(uint32_t timeoutMs);
    void deriveApplicationKeys();
    bool sendClientFinished();

    void setSendKeys(const uint8_t trafficSecret[32]);
    void setRecvKeys(const uint8_t trafficSecret[32]);
};

#endif // TLS_HPP
