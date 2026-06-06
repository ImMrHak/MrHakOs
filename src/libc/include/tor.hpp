#ifndef TOR_HPP
#define TOR_HPP

#include <stdint.h>
#include <network.hpp>
#include <tls.hpp>
#include <crypto.hpp>

// Native Tor client: link handshake + ntor circuit handshake + RELAY cells,
// running over the TlsClient transport. One circuit at a time.

// ntor onion handshake (tor-spec 5.1.4). Pure functions so they can be unit
// tested offline against a reference ntor responder.
//   hdata84 receives NODEID(20) || B(32) || X(32) for the CREATE2 cell.
void ntor_client_begin(const uint8_t nodeId[20], const uint8_t B[32],
                       uint8_t xPriv[32], uint8_t X[32], uint8_t hdata84[84]);
//   Verify the responder's reply (Y, AUTH) and, on success, derive 72 bytes of
//   key material (Df[20] | Db[20] | Kf[16] | Kb[16]). Returns false if AUTH fails.
bool ntor_client_finish(const uint8_t nodeId[20], const uint8_t B[32],
                        const uint8_t xPriv[32], const uint8_t X[32],
                        const uint8_t Y[32], const uint8_t AUTH[32],
                        uint8_t keyMaterial[72]);

struct TorHop {
    Sha1Ctx fwdDigest;     // running SHA-1 over relay cells we send
    Sha1Ctx bwdDigest;     // running SHA-1 over relay cells we receive
    AesCtrState fwdCipher; // AES-128-CTR forward
    AesCtrState bwdCipher; // AES-128-CTR backward
    uint8_t rsaId[20];      // this hop's RSA identity digest
    uint8_t ntorKey[32];    // this hop's ntor onion key
    bool established;
};

class TorClient {
public:
    void reset(Network* net);

    // Identify the guard (from the consensus) before connecting.
    void setGuard(uint32_t ip, uint16_t orPort, const uint8_t id20[20]);
    // Provide the guard's ntor onion key directly (instead of fetching a
    // descriptor) when it is already known.
    void setNtorKey(const uint8_t key32[32]);
    // Fetch the guard's server descriptor from a directory cache to obtain its
    // ntor onion key (needs setGuard first, for the fingerprint URL).
    bool fetchGuardDescriptor(uint32_t dirIp, uint16_t dirPort);

    bool openTls(uint32_t timeoutMs);          // TCP + TLS to guard ORPort
    bool linkHandshake(uint32_t timeoutMs);    // VERSIONS / CERTS / NETINFO
    bool createCircuit(uint32_t timeoutMs);    // ntor CREATE2 / CREATED2 to guard

    // Extend the circuit one more hop (RELAY_EARLY EXTEND2 -> EXTENDED2).
    bool extendCircuit(uint32_t ip, uint16_t orPort, const uint8_t id20[20],
                       const uint8_t ntorKey[32], uint32_t timeoutMs);

    // Open a directory stream to the last hop and fetch `path` over it; proves
    // the RELAY stream layer end to end. Returns bytes captured into out.
    uint32_t beginDirFetch(const char* path, uint8_t* out, uint32_t outCap, uint32_t timeoutMs);

    // Open an arbitrary TCP stream through the last hop (normally an Exit) using
    // RELAY_BEGIN, send a simple HTTP/1.0 GET, and capture RELAY_DATA bytes.
    // The host is sent as a domain string so DNS resolution happens at the exit;
    // callers must not perform direct guest DNS as a fallback.
    uint32_t beginTcpFetch(const char* host, uint16_t port, const char* path,
                           uint8_t* out, uint32_t outCap, uint32_t timeoutMs);

    void close();

    // ----- diagnostics -----
    const char* stageName() const { return stage; }
    bool haveNtorKey() const { return haveKey; }
    const uint8_t* guardNtorKey() const { return ntorKey; }
    int linkVersion() const { return linkVer; }
    bool sawVersions() const { return gotVersions; }
    bool sawCerts() const { return gotCerts; }
    bool sawAuthChallenge() const { return gotAuthChallenge; }
    bool sawNetinfo() const { return gotNetinfo; }
    bool sawCreated2() const { return gotCreated2; }
    bool ntorOk() const { return ntorVerified; }
    uint16_t certCount() const { return certs; }
    uint32_t circuitId() const { return circId; }
    int hopCount() const { return numHops; }
    const uint8_t* circuitKeyProof() const { return keyProof; } // Df of hop 1 (verifiable)
    TlsClient& tlsClient() { return tls; }

private:
    Network* net = nullptr;
    TlsClient tls;
    uint32_t guardIp = 0;
    uint16_t orPort = 0;
    uint8_t guardId[20];
    uint8_t ntorKey[32];
    bool haveKey = false;

    int linkVer = 0;
    uint8_t circIdLen = 2;
    uint32_t circId = 0;

    uint8_t ephPriv[32];
    uint8_t ephPub[32];

    TorHop hops[3];
    int numHops = 0;

    uint8_t keyProof[20];

    const char* stage = "idle";
    bool gotVersions = false, gotCerts = false, gotAuthChallenge = false;
    bool gotNetinfo = false, gotCreated2 = false, ntorVerified = false;
    uint16_t certs = 0;

    // cell I/O
    bool sendFixedCell(uint32_t circ, uint8_t cmd, const uint8_t* payload509);
    bool sendVarCell(uint32_t circ, uint8_t cmd, const uint8_t* data, uint16_t len);
    bool readCell(uint32_t* circOut, uint8_t* cmdOut, uint8_t* payload,
                  uint32_t payloadCap, uint32_t* payloadLenOut, uint32_t timeoutMs);
    bool sendNetinfo();
    bool sendVersions();

    // RELAY cells through the established hops
    bool sendRelayCell(uint8_t relayCmd, uint16_t streamId, const uint8_t* data, uint16_t len, bool early);
    bool recvRelayCell(uint8_t* relayCmdOut, uint16_t* streamIdOut, uint8_t* data,
                       uint16_t* lenOut, uint32_t timeoutMs);
};

#endif // TOR_HPP
