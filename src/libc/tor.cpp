#include <tor.hpp>
#include <string.hpp>
#include <serial.hpp>

// ===========================================================================
// Encoding helpers
// ===========================================================================
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64_decode(const char* in, int inLen, uint8_t* out, int outCap) {
    int acc = 0, bits = 0, o = 0;
    for (int i = 0; i < inLen; i++) {
        int v = b64val(in[i]);
        if (v < 0) continue; // skip '=', whitespace, newlines
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; if (o < outCap) out[o++] = static_cast<uint8_t>((acc >> bits) & 0xff); }
    }
    return o;
}

static void bytes_to_hex_upper(const uint8_t* in, int len, char* out) {
    static const char* H = "0123456789ABCDEF";
    for (int i = 0; i < len; i++) { out[2 * i] = H[in[i] >> 4]; out[2 * i + 1] = H[in[i] & 0xf]; }
    out[2 * len] = '\0';
}

static void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }
static void put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);  p[3] = static_cast<uint8_t>(v);
}

// ===========================================================================
// ntor handshake (tor-spec 5.1.4)
// ===========================================================================
static const char NTOR_PROTOID[] = "ntor-curve25519-sha256-1";
static const uint32_t NTOR_PROTOID_LEN = 24;

static void ntor_hmac(const char* keySuffix, const uint8_t* msg, uint32_t msgLen, uint8_t out[32]) {
    uint8_t key[64];
    uint32_t k = 0;
    for (uint32_t i = 0; i < NTOR_PROTOID_LEN; i++) key[k++] = static_cast<uint8_t>(NTOR_PROTOID[i]);
    for (uint32_t i = 0; keySuffix[i]; i++) key[k++] = static_cast<uint8_t>(keySuffix[i]);
    hmac_sha256(key, k, msg, msgLen, out);
}

void ntor_client_begin(const uint8_t nodeId[20], const uint8_t B[32],
                       uint8_t xPriv[32], uint8_t X[32], uint8_t hdata84[84]) {
    rng_bytes(xPriv, 32);
    x25519_base(X, xPriv);
    for (int i = 0; i < 20; i++) hdata84[i] = nodeId[i];
    for (int i = 0; i < 32; i++) hdata84[20 + i] = B[i];
    for (int i = 0; i < 32; i++) hdata84[52 + i] = X[i];
}

static bool all_zero(const uint8_t* p, int n) {
    uint8_t d = 0; for (int i = 0; i < n; i++) d |= p[i]; return d == 0;
}

bool ntor_client_finish(const uint8_t nodeId[20], const uint8_t B[32],
                        const uint8_t xPriv[32], const uint8_t X[32],
                        const uint8_t Y[32], const uint8_t AUTH[32],
                        uint8_t keyMaterial[72]) {
    uint8_t xy[32], xb[32];
    x25519_scalarmult(xy, xPriv, Y);
    x25519_scalarmult(xb, xPriv, B);
    if (all_zero(xy, 32) || all_zero(xb, 32)) return false; // reject low-order points

    // secret_input = xy | xb | ID | B | X | Y | PROTOID
    uint8_t si[32 + 32 + 20 + 32 + 32 + 32 + 24];
    uint32_t p = 0;
    for (int i = 0; i < 32; i++) si[p++] = xy[i];
    for (int i = 0; i < 32; i++) si[p++] = xb[i];
    for (int i = 0; i < 20; i++) si[p++] = nodeId[i];
    for (int i = 0; i < 32; i++) si[p++] = B[i];
    for (int i = 0; i < 32; i++) si[p++] = X[i];
    for (int i = 0; i < 32; i++) si[p++] = Y[i];
    for (uint32_t i = 0; i < NTOR_PROTOID_LEN; i++) si[p++] = static_cast<uint8_t>(NTOR_PROTOID[i]);

    uint8_t keySeed[32], verify[32];
    ntor_hmac(":key_extract", si, p, keySeed);
    ntor_hmac(":verify", si, p, verify);

    // auth_input = verify | ID | B | Y | X | PROTOID | "Server"
    uint8_t ai[32 + 20 + 32 + 32 + 32 + 24 + 6];
    uint32_t a = 0;
    for (int i = 0; i < 32; i++) ai[a++] = verify[i];
    for (int i = 0; i < 20; i++) ai[a++] = nodeId[i];
    for (int i = 0; i < 32; i++) ai[a++] = B[i];
    for (int i = 0; i < 32; i++) ai[a++] = Y[i];
    for (int i = 0; i < 32; i++) ai[a++] = X[i];
    for (uint32_t i = 0; i < NTOR_PROTOID_LEN; i++) ai[a++] = static_cast<uint8_t>(NTOR_PROTOID[i]);
    const char* srv = "Server";
    for (int i = 0; i < 6; i++) ai[a++] = static_cast<uint8_t>(srv[i]);

    uint8_t authExpect[32];
    ntor_hmac(":mac", ai, a, authExpect);
    if (!crypto_equal(authExpect, AUTH, 32)) return false;

    // KDF-RFC5869: PRK = keySeed, info = PROTOID || ":key_expand"
    uint8_t mexpand[24 + 11];
    uint32_t m = 0;
    for (uint32_t i = 0; i < NTOR_PROTOID_LEN; i++) mexpand[m++] = static_cast<uint8_t>(NTOR_PROTOID[i]);
    const char* ke = ":key_expand";
    for (int i = 0; ke[i]; i++) mexpand[m++] = static_cast<uint8_t>(ke[i]);
    hkdf_sha256_expand(keySeed, mexpand, m, keyMaterial, 72);
    return true;
}

// ===========================================================================
// TorClient
// ===========================================================================
static uint8_t g_dirResp[16384];   // descriptor fetch buffer
static uint8_t g_varCell[8192];    // received variable-length cell payload
static uint8_t g_cellPayload[1024]; // received fixed cell payload (509)

void TorClient::reset(Network* n) {
    net = n;
    tls.reset(n);
    guardIp = 0; orPort = 0; haveKey = false;
    linkVer = 0; circIdLen = 2; circId = 0;
    numHops = 0;
    stage = "idle";
    gotVersions = gotCerts = gotAuthChallenge = false;
    gotNetinfo = gotCreated2 = ntorVerified = false;
    certs = 0;
    for (int i = 0; i < 3; i++) hops[i].established = false;
}

void TorClient::setGuard(uint32_t ip, uint16_t op, const uint8_t id20[20]) {
    guardIp = ip; orPort = op;
    for (int i = 0; i < 20; i++) guardId[i] = id20[i];
}

void TorClient::setNtorKey(const uint8_t key32[32]) {
    for (int i = 0; i < 32; i++) ntorKey[i] = key32[i];
    haveKey = true;
}

bool TorClient::fetchGuardDescriptor(uint32_t dirIp, uint16_t dirPort) {
    stage = "fetch_descriptor";
    char fp[41];
    bytes_to_hex_upper(guardId, 20, fp);
    char req[160];
    int r = 0;
    const char* a = "GET /tor/server/fp/";
    for (int i = 0; a[i]; i++) req[r++] = a[i];
    for (int i = 0; fp[i]; i++) req[r++] = fp[i];
    const char* b = " HTTP/1.0\r\nConnection: close\r\n\r\n";
    for (int i = 0; b[i]; i++) req[r++] = b[i];
    req[r] = '\0';

    uint16_t got = 0;
    if (!net->tcpRequestRaw(dirIp, dirPort, reinterpret_cast<const uint8_t*>(req), static_cast<uint16_t>(r),
                            g_dirResp, sizeof(g_dirResp) - 1, &got)) {
        stage = "descriptor_http_fail";
        return false;
    }
    g_dirResp[got] = 0;
    // Find the "ntor-onion-key <base64>" line. The field name must be followed
    // by whitespace so we do NOT match the earlier "ntor-onion-key-crosscert"
    // line (of which "ntor-onion-key" is a prefix).
    const char* needle = "ntor-onion-key";
    int nlen = 14;
    int pos = -1;
    for (int i = 0; i + nlen + 1 <= static_cast<int>(got); i++) {
        bool m = true;
        for (int j = 0; j < nlen; j++) if (g_dirResp[i + j] != static_cast<uint8_t>(needle[j])) { m = false; break; }
        if (!m) continue;
        uint8_t after = g_dirResp[i + nlen];
        if (after == ' ' || after == '\t' || after == '\r' || after == '\n') { pos = i + nlen; break; }
    }
    if (pos < 0) { stage = "no_ntor_key"; return false; }
    while (pos < static_cast<int>(got) && (g_dirResp[pos] == ' ' || g_dirResp[pos] == '\r' || g_dirResp[pos] == '\n')) pos++;
    int start = pos;
    while (pos < static_cast<int>(got) && b64val(static_cast<char>(g_dirResp[pos])) >= 0) pos++;
    int n = base64_decode(reinterpret_cast<const char*>(g_dirResp + start), pos - start, ntorKey, 32);
    if (n != 32) { stage = "bad_ntor_key"; return false; }
    haveKey = true;
    stage = "descriptor_ok";
    return true;
}

bool TorClient::openTls(uint32_t timeoutMs) {
    stage = "tcp_connect";
    if (!net->tcpStreamOpen(guardIp, orPort)) { stage = "tcp_fail"; return false; }
    stage = "tls_handshake";
    if (!tls.handshake(timeoutMs)) { stage = tls.stageName(); net->tcpStreamClose(); return false; }
    stage = "tls_ok";
    return true;
}

// --------------------------------------------------------------------------
// Cell I/O
// --------------------------------------------------------------------------
bool TorClient::sendVarCell(uint32_t circ, uint8_t cmd, const uint8_t* data, uint16_t len) {
    uint8_t hdr[7];
    uint32_t h = 0;
    if (circIdLen == 4) { put32(hdr, circ); h = 4; } else { put16(hdr, static_cast<uint16_t>(circ)); h = 2; }
    hdr[h++] = cmd;
    put16(hdr + h, len); h += 2;
    if (!tls.write(hdr, h)) return false;
    if (len && !tls.write(data, len)) return false;
    return true;
}

bool TorClient::sendFixedCell(uint32_t circ, uint8_t cmd, const uint8_t* payload509) {
    uint8_t cell[5 + 509];
    uint32_t h = 0;
    if (circIdLen == 4) { put32(cell, circ); h = 4; } else { put16(cell, static_cast<uint16_t>(circ)); h = 2; }
    cell[h++] = cmd;
    for (int i = 0; i < 509; i++) cell[h + i] = payload509[i];
    return tls.write(cell, h + 509);
}

bool TorClient::readCell(uint32_t* circOut, uint8_t* cmdOut, uint8_t* payload,
                         uint32_t payloadCap, uint32_t* payloadLenOut, uint32_t timeoutMs) {
    uint8_t idbuf[4];
    if (tls.read(idbuf, circIdLen, timeoutMs) != circIdLen) return false;
    uint32_t circ = (circIdLen == 4)
        ? ((static_cast<uint32_t>(idbuf[0]) << 24) | (static_cast<uint32_t>(idbuf[1]) << 16) |
           (static_cast<uint32_t>(idbuf[2]) << 8) | idbuf[3])
        : ((static_cast<uint32_t>(idbuf[0]) << 8) | idbuf[1]);
    uint8_t cmd;
    if (tls.read(&cmd, 1, timeoutMs) != 1) return false;
    bool var = (cmd == 7) || (cmd >= 128);
    uint32_t len;
    if (var) {
        uint8_t l[2];
        if (tls.read(l, 2, timeoutMs) != 2) return false;
        len = (static_cast<uint32_t>(l[0]) << 8) | l[1];
        uint32_t take = len < payloadCap ? len : payloadCap;
        if (take && tls.read(payload, take, timeoutMs) != take) return false;
        // Drain any overflow to keep the stream aligned.
        uint32_t rem = len - take;
        while (rem) {
            uint8_t scratch[256];
            uint32_t chunk = rem < sizeof(scratch) ? rem : sizeof(scratch);
            if (tls.read(scratch, chunk, timeoutMs) != chunk) return false;
            rem -= chunk;
        }
        len = take;
    } else {
        if (payloadCap < 509) return false;
        if (tls.read(payload, 509, timeoutMs) != 509) return false;
        len = 509;
    }
    *circOut = circ; *cmdOut = cmd; *payloadLenOut = len;
    return true;
}

bool TorClient::sendVersions() {
    uint8_t v[6] = {0x00, 0x03, 0x00, 0x04, 0x00, 0x05}; // propose link versions 3,4,5
    circIdLen = 2; // VERSIONS always uses a 2-byte circuit id
    return sendVarCell(0, 7, v, 6);
}

bool TorClient::sendNetinfo() {
    uint8_t payload[509];
    for (int i = 0; i < 509; i++) payload[i] = 0;
    // TIME(4)=0 (clients may send 0). OTHERADDR = guard IPv4. NMYADDR=0.
    uint32_t p = 4;
    payload[p++] = 0x04; // ATYPE IPv4
    payload[p++] = 4;    // ALEN
    put32(payload + p, guardIp); p += 4;
    payload[p++] = 0;    // NMYADDR
    return sendFixedCell(0, 8, payload);
}

bool TorClient::linkHandshake(uint32_t timeoutMs) {
    stage = "versions";
    if (!sendVersions()) { stage = "versions_send_fail"; return false; }

    // Read the server flight until we have its NETINFO.
    for (int guard = 0; guard < 24 && !gotNetinfo; guard++) {
        uint32_t circ, plen; uint8_t cmd;
        if (!readCell(&circ, &cmd, g_varCell, sizeof(g_varCell), &plen, timeoutMs)) { stage = "link_read_fail"; return false; }
        if (cmd == 7) { // VERSIONS
            int best = 0;
            for (uint32_t i = 0; i + 1 < plen; i += 2) {
                int ver = (g_varCell[i] << 8) | g_varCell[i + 1];
                if ((ver == 3 || ver == 4 || ver == 5) && ver > best) best = ver;
            }
            linkVer = best ? best : 3;
            circIdLen = (linkVer >= 4) ? 4 : 2;
            gotVersions = true;
        } else if (cmd == 129) { // CERTS
            gotCerts = true;
            if (plen >= 1) certs = g_varCell[0];
        } else if (cmd == 130) { // AUTH_CHALLENGE
            gotAuthChallenge = true;
        } else if (cmd == 8) { // NETINFO
            gotNetinfo = true;
        }
        // ignore PADDING / others
    }
    if (!gotVersions || !gotNetinfo) { stage = "link_incomplete"; return false; }
    stage = "send_netinfo";
    if (!sendNetinfo()) { stage = "netinfo_send_fail"; return false; }
    stage = "link_ok";
    return true;
}

bool TorClient::createCircuit(uint32_t timeoutMs) {
    if (!haveKey) { stage = "no_ntor_key"; return false; }
    // Choose an initiator circuit id (high bit set for v4+).
    uint8_t rnd[4]; rng_bytes(rnd, 4);
    if (circIdLen == 4) {
        circId = 0x80000000u | ((static_cast<uint32_t>(rnd[0]) << 16) | (rnd[1] << 8) | rnd[2]);
        if (circId == 0) circId = 0x80000001u;
    } else {
        circId = 0x8000u | (rnd[0] << 8 | rnd[1]) >> 1;
        if (circId == 0) circId = 0x8001u;
    }

    uint8_t hdata[84];
    ntor_client_begin(guardId, ntorKey, ephPriv, ephPub, hdata);

    uint8_t payload[509];
    for (int i = 0; i < 509; i++) payload[i] = 0;
    put16(payload, 0x0002);     // HTYPE = ntor
    put16(payload + 2, 84);     // HLEN
    for (int i = 0; i < 84; i++) payload[4 + i] = hdata[i];
    stage = "create2";
    if (!sendFixedCell(circId, 10, payload)) { stage = "create2_send_fail"; return false; }

    // Await CREATED2 on our circuit.
    for (int guard = 0; guard < 16; guard++) {
        uint32_t circ, plen; uint8_t cmd;
        if (!readCell(&circ, &cmd, g_cellPayload, sizeof(g_cellPayload), &plen, timeoutMs)) { stage = "created2_read_fail"; return false; }
        if (cmd == 8) continue; // a late NETINFO
        if (circ != circId) continue;
        if (cmd == 4) { stage = "circuit_destroyed"; return false; } // DESTROY
        if (cmd == 11) { // CREATED2
            uint16_t hlen = static_cast<uint16_t>((g_cellPayload[0] << 8) | g_cellPayload[1]);
            if (hlen < 64) { stage = "created2_short"; return false; }
            const uint8_t* Y = g_cellPayload + 2;
            const uint8_t* AUTH = g_cellPayload + 2 + 32;
            gotCreated2 = true;
            uint8_t km[72];
            if (!ntor_client_finish(guardId, ntorKey, ephPriv, ephPub, Y, AUTH, km)) { stage = "ntor_auth_fail"; return false; }
            // Set up hop 0 (guard): Df|Db|Kf|Kb.
            TorHop& hop = hops[0];
            sha1_init(&hop.fwdDigest); sha1_update(&hop.fwdDigest, km, 20);
            sha1_init(&hop.bwdDigest); sha1_update(&hop.bwdDigest, km + 20, 20);
            uint8_t zeroIv[16]; for (int i = 0; i < 16; i++) zeroIv[i] = 0;
            aes128_ctr_init(&hop.fwdCipher, km + 40, zeroIv);
            aes128_ctr_init(&hop.bwdCipher, km + 56, zeroIv);
            for (int i = 0; i < 20; i++) hop.rsaId[i] = guardId[i];
            for (int i = 0; i < 32; i++) hop.ntorKey[i] = ntorKey[i];
            hop.established = true;
            numHops = 1;
            for (int i = 0; i < 20; i++) keyProof[i] = km[i]; // Df, a verifiable artifact
            ntorVerified = true;
            stage = "circuit_ok";
            Serial::writeString("[tor] ntor circuit established (guard)\n");
            return true;
        }
    }
    stage = "created2_timeout";
    return false;
}

// --------------------------------------------------------------------------
// RELAY cells
// --------------------------------------------------------------------------
bool TorClient::sendRelayCell(uint8_t relayCmd, uint16_t streamId, const uint8_t* data, uint16_t len, bool early) {
    if (numHops == 0 || len > 498) return false;
    int dest = numHops - 1;
    uint8_t payload[509];
    for (int i = 0; i < 509; i++) payload[i] = 0;
    payload[0] = relayCmd;
    put16(payload + 1, 0x0000);        // recognized
    put16(payload + 3, streamId);
    // digest field (5..8) stays zero for the computation
    put16(payload + 9, len);
    for (int i = 0; i < len; i++) payload[11 + i] = data[i];

    // digest = first 4 bytes of running forward SHA-1 (over this payload, digest=0)
    Sha1Ctx tmp = hops[dest].fwdDigest;
    sha1_update(&tmp, payload, 509);
    hops[dest].fwdDigest = tmp; // commit running digest
    uint8_t dg[20]; Sha1Ctx fin = tmp; sha1_final(&fin, dg);
    for (int i = 0; i < 4; i++) payload[5 + i] = dg[i];

    // onion-encrypt from destination hop inward to the guard
    for (int h = dest; h >= 0; h--) aes128_ctr_crypt(&hops[h].fwdCipher, payload, payload, 509);

    return sendFixedCell(circId, early ? 9 : 3, payload);
}

bool TorClient::recvRelayCell(uint8_t* relayCmdOut, uint16_t* streamIdOut, uint8_t* data,
                              uint16_t* lenOut, uint32_t timeoutMs) {
    for (int guard = 0; guard < 64; guard++) {
        uint32_t circ, plen; uint8_t cmd;
        if (!readCell(&circ, &cmd, g_cellPayload, sizeof(g_cellPayload), &plen, timeoutMs)) return false;
        if (circ != circId) continue;
        if (cmd == 4) return false; // DESTROY
        if (cmd != 3 && cmd != 9) continue; // not a RELAY cell
        uint8_t payload[509];
        for (int i = 0; i < 509; i++) payload[i] = g_cellPayload[i];
        // Peel each hop's backward layer; the originating hop recognizes the cell.
        for (int h = 0; h < numHops; h++) {
            aes128_ctr_crypt(&hops[h].bwdCipher, payload, payload, 509);
            if (payload[1] == 0 && payload[2] == 0) { // recognized
                uint8_t saved[4];
                for (int i = 0; i < 4; i++) { saved[i] = payload[5 + i]; payload[5 + i] = 0; }
                Sha1Ctx tmp = hops[h].bwdDigest;
                sha1_update(&tmp, payload, 509);
                uint8_t dg[20]; Sha1Ctx fin = tmp; sha1_final(&fin, dg);
                if (dg[0] == saved[0] && dg[1] == saved[1] && dg[2] == saved[2] && dg[3] == saved[3]) {
                    hops[h].bwdDigest = tmp; // commit
                    *relayCmdOut = payload[0];
                    *streamIdOut = static_cast<uint16_t>((payload[3] << 8) | payload[4]);
                    uint16_t rlen = static_cast<uint16_t>((payload[9] << 8) | payload[10]);
                    if (rlen > 498) rlen = 498;
                    if (data) for (int i = 0; i < rlen; i++) data[i] = payload[11 + i];
                    *lenOut = rlen;
                    return true;
                }
                // not ours: restore digest bytes and keep peeling
                for (int i = 0; i < 4; i++) payload[5 + i] = saved[i];
            }
        }
        // unrecognized at every hop: ignore and read the next cell
    }
    return false;
}

bool TorClient::extendCircuit(uint32_t ip, uint16_t op, const uint8_t id20[20],
                              const uint8_t nkey[32], uint32_t timeoutMs) {
    if (numHops == 0 || numHops >= 3) { stage = "extend_state"; return false; }
    stage = "extend2";
    uint8_t x[32], X[32], hdata[84];
    ntor_client_begin(id20, nkey, x, X, hdata);

    uint8_t body[256];
    uint32_t p = 0;
    body[p++] = 2; // NSPEC: IPv4 + legacy RSA id
    body[p++] = 0; body[p++] = 6; put32(body + p, ip); p += 4; put16(body + p, op); p += 2;
    body[p++] = 2; body[p++] = 20; for (int i = 0; i < 20; i++) body[p++] = id20[i];
    put16(body + p, 0x0002); p += 2; // HTYPE ntor
    put16(body + p, 84); p += 2;     // HLEN
    for (int i = 0; i < 84; i++) body[p++] = hdata[i];

    if (!sendRelayCell(14 /*EXTEND2*/, 0, body, static_cast<uint16_t>(p), true)) { stage = "extend2_send_fail"; return false; }

    uint8_t rcmd; uint16_t sid; uint8_t rdata[498]; uint16_t rlen;
    if (!recvRelayCell(&rcmd, &sid, rdata, &rlen, timeoutMs)) { stage = "extended2_timeout"; return false; }
    if (rcmd != 15 /*EXTENDED2*/) { stage = "not_extended2"; return false; }
    uint16_t hlen = static_cast<uint16_t>((rdata[0] << 8) | rdata[1]);
    if (hlen < 64) { stage = "extended2_short"; return false; }
    const uint8_t* Y = rdata + 2;
    const uint8_t* AUTH = rdata + 2 + 32;
    uint8_t km[72];
    if (!ntor_client_finish(id20, nkey, x, X, Y, AUTH, km)) { stage = "extend_ntor_fail"; return false; }
    TorHop& hop = hops[numHops];
    sha1_init(&hop.fwdDigest); sha1_update(&hop.fwdDigest, km, 20);
    sha1_init(&hop.bwdDigest); sha1_update(&hop.bwdDigest, km + 20, 20);
    uint8_t zeroIv[16]; for (int i = 0; i < 16; i++) zeroIv[i] = 0;
    aes128_ctr_init(&hop.fwdCipher, km + 40, zeroIv);
    aes128_ctr_init(&hop.bwdCipher, km + 56, zeroIv);
    for (int i = 0; i < 20; i++) hop.rsaId[i] = id20[i];
    hop.established = true;
    numHops++;
    stage = "extend_ok";
    return true;
}

uint32_t TorClient::beginDirFetch(const char* path, uint8_t* out, uint32_t outCap, uint32_t timeoutMs) {
    if (numHops == 0) return 0;
    stage = "begin_dir";
    uint16_t streamId = 1;
    if (!sendRelayCell(13 /*RELAY_BEGIN_DIR*/, streamId, nullptr, 0, false)) { stage = "begindir_send_fail"; return 0; }
    uint8_t rcmd; uint16_t sid; uint8_t rdata[498]; uint16_t rlen;
    if (!recvRelayCell(&rcmd, &sid, rdata, &rlen, timeoutMs)) { stage = "connected_timeout"; return 0; }
    if (rcmd != 4 /*CONNECTED*/) {
        if (rcmd == 3) stage = "begindir_refused"; else stage = "begindir_unexpected";
        return 0;
    }
    // Send the HTTP request as RELAY_DATA.
    char req[160]; int r = 0;
    const char* g = "GET ";
    for (int i = 0; g[i]; i++) req[r++] = g[i];
    for (int i = 0; path[i] && r < static_cast<int>(sizeof(req)) - 20; i++) req[r++] = path[i];
    const char* h = " HTTP/1.0\r\n\r\n";
    for (int i = 0; h[i] && r < static_cast<int>(sizeof(req)) - 1; i++) req[r++] = h[i];
    if (!sendRelayCell(2 /*RELAY_DATA*/, streamId, reinterpret_cast<const uint8_t*>(req), static_cast<uint16_t>(r), false)) { stage = "data_send_fail"; return 0; }

    uint32_t total = 0;
    for (int i = 0; i < 64; i++) {
        if (!recvRelayCell(&rcmd, &sid, rdata, &rlen, timeoutMs)) break;
        if (sid != streamId) continue;
        if (rcmd == 2) { // RELAY_DATA
            for (uint16_t j = 0; j < rlen && total < outCap; j++) out[total++] = rdata[j];
        } else if (rcmd == 3) { // RELAY_END
            break;
        }
    }
    stage = "begindir_ok";
    return total;
}

uint32_t TorClient::beginTcpFetch(const char* host, uint16_t port, const char* path,
                                  uint8_t* out, uint32_t outCap, uint32_t timeoutMs) {
    if (numHops == 0 || !host || !host[0] || !path) return 0;
    stage = "begin_tcp";
    uint16_t streamId = 3;

    uint8_t begin[256];
    uint32_t b = 0;
    for (int i = 0; host[i] && b < sizeof(begin) - 12; i++) begin[b++] = static_cast<uint8_t>(host[i]);
    begin[b++] = ':';
    char portBuf[6]; int pn = 0;
    uint16_t tmp = port;
    char rev[6]; int rn = 0;
    if (tmp == 0) rev[rn++] = '0';
    while (tmp && rn < 5) { rev[rn++] = static_cast<char>('0' + (tmp % 10)); tmp = static_cast<uint16_t>(tmp / 10); }
    while (rn && pn < 5) portBuf[pn++] = rev[--rn];
    portBuf[pn] = '\0';
    for (int i = 0; portBuf[i] && b < sizeof(begin) - 2; i++) begin[b++] = static_cast<uint8_t>(portBuf[i]);
    begin[b++] = 0; // Tor RELAY_BEGIN address string terminator; no flags.

    if (!sendRelayCell(1 /*RELAY_BEGIN*/, streamId, begin, static_cast<uint16_t>(b), false)) { stage = "begin_send_fail"; return 0; }
    uint8_t rcmd; uint16_t sid; uint8_t rdata[498]; uint16_t rlen;
    for (;;) {
        if (!recvRelayCell(&rcmd, &sid, rdata, &rlen, timeoutMs)) { stage = "tcp_connected_timeout"; return 0; }
        if (sid != streamId) continue;
        if (rcmd == 4 /*CONNECTED*/) break;
        if (rcmd == 3 /*END*/) { stage = "tcp_begin_refused"; return 0; }
        stage = "tcp_begin_unexpected"; return 0;
    }

    char req[384]; int r = 0;
    const char* a = "GET ";
    for (int i = 0; a[i] && r < static_cast<int>(sizeof(req)) - 1; i++) req[r++] = a[i];
    for (int i = 0; path[i] && r < static_cast<int>(sizeof(req)) - 1; i++) req[r++] = path[i];
    const char* mid = " HTTP/1.0\r\nHost: ";
    for (int i = 0; mid[i] && r < static_cast<int>(sizeof(req)) - 1; i++) req[r++] = mid[i];
    for (int i = 0; host[i] && r < static_cast<int>(sizeof(req)) - 1; i++) req[r++] = host[i];
    const char* tail = "\r\nUser-Agent: MrHakOS-Tor/0.2\r\nConnection: close\r\n\r\n";
    for (int i = 0; tail[i] && r < static_cast<int>(sizeof(req)) - 1; i++) req[r++] = tail[i];
    if (!sendRelayCell(2 /*RELAY_DATA*/, streamId, reinterpret_cast<const uint8_t*>(req), static_cast<uint16_t>(r), false)) { stage = "tcp_data_send_fail"; return 0; }

    uint32_t total = 0;
    for (int i = 0; i < 96; i++) {
        if (!recvRelayCell(&rcmd, &sid, rdata, &rlen, timeoutMs)) break;
        if (sid != streamId) continue;
        if (rcmd == 2 /*DATA*/) {
            for (uint16_t j = 0; j < rlen && total < outCap; j++) out[total++] = rdata[j];
        } else if (rcmd == 3 /*END*/) {
            break;
        }
    }
    stage = "tcp_stream_ok";
    return total;
}

void TorClient::close() {
    if (net) net->tcpStreamClose();
    numHops = 0;
}
