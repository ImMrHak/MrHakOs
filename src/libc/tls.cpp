#include <tls.hpp>
#include <crypto.hpp>
#include <string.hpp>
#include <serial.hpp>

// TLS 1.3 client (RFC 8446) for exactly TLS_AES_128_GCM_SHA256 over x25519.
// See tls.hpp for the security rationale (Tor authenticates the relay itself).

static const uint8_t TLS_LEGACY_VERSION_HI = 0x03;
static const uint8_t TLS_LEGACY_VERSION_LO = 0x03; // record/legacy version 0x0303

// Record content types
static const uint8_t REC_CHANGE_CIPHER = 0x14;
static const uint8_t REC_ALERT         = 0x15;
static const uint8_t REC_HANDSHAKE     = 0x16;
static const uint8_t REC_APPDATA       = 0x17;

// Handshake message types
static const uint8_t HS_CLIENT_HELLO          = 0x01;
static const uint8_t HS_SERVER_HELLO          = 0x02;
static const uint8_t HS_NEW_SESSION_TICKET    = 0x04;
static const uint8_t HS_ENCRYPTED_EXTENSIONS  = 0x08;
static const uint8_t HS_CERTIFICATE           = 0x0b;
static const uint8_t HS_CERTIFICATE_VERIFY    = 0x0f;
static const uint8_t HS_FINISHED              = 0x14;

// SHA-256 of the empty string (Transcript-Hash of no messages, used by "derived").
static const uint8_t SHA256_EMPTY[32] = {
    0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
    0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
};

// Scratch buffers in .bss (one connection at a time, single-threaded).
static uint8_t g_rec[20480];     // raw record body read off the wire
static uint8_t g_hs[20480];      // accumulated plaintext handshake bytes
static uint8_t g_plain[20480];   // decrypted record plaintext
static uint8_t g_out[20480];     // outgoing record assembly
static uint8_t g_appBuf[20480];  // decrypted application-data for read()
static Sha256Ctx g_transcript;   // running hash of handshake messages

static void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }

static void transcriptHashNow(uint8_t out[32]) { Sha256Ctx c = g_transcript; sha256_final(&c, out); }

// HKDF-Expand-Label (RFC 8446 7.1).
static void hkdfExpandLabel(const uint8_t secret[32], const char* label,
                            const uint8_t* ctx, uint32_t ctxLen, uint8_t* out, uint32_t outLen) {
    uint8_t info[2 + 1 + 64 + 1 + 64];
    uint32_t p = 0;
    info[p++] = static_cast<uint8_t>(outLen >> 8);
    info[p++] = static_cast<uint8_t>(outLen);
    // label = "tls13 " + label
    uint8_t fullLen = 6;
    for (const char* q = label; *q; q++) fullLen++;
    info[p++] = fullLen;
    const char* pre = "tls13 ";
    for (int i = 0; i < 6; i++) info[p++] = static_cast<uint8_t>(pre[i]);
    for (const char* q = label; *q; q++) info[p++] = static_cast<uint8_t>(*q);
    info[p++] = static_cast<uint8_t>(ctxLen);
    for (uint32_t i = 0; i < ctxLen; i++) info[p++] = ctx[i];
    hkdf_sha256_expand(secret, info, p, out, outLen);
}

// Derive-Secret(secret, label, messages) with context = Transcript-Hash(messages).
static void deriveSecret(const uint8_t secret[32], const char* label, const uint8_t th[32], uint8_t out[32]) {
    hkdfExpandLabel(secret, label, th, 32, out, 32);
}

void TlsClient::reset(Network* n) {
    net = n;
    est = false;
    stage = "idle";
    sendSeq = recvSeq = 0;
    haveSendKeys = haveRecvKeys = false;
    appPos = appLen = 0;
    suite = group = 0;
    gotServerHello = gotKeyShare = gotEncryptedExtensions = false;
    gotCertificate = gotServerFinished = false;
    certBytes = 0;
    alertLvl = alertDesc = 0;
}

// ---------------------------------------------------------------------------
// Record layer
// ---------------------------------------------------------------------------
bool TlsClient::sendPlainRecord(uint8_t type, const uint8_t* data, uint32_t len) {
    if (len + 5 > sizeof(g_out)) return false;
    g_out[0] = type;
    g_out[1] = TLS_LEGACY_VERSION_HI;
    // The initial ClientHello record uses legacy version 0x0301 for maximum
    // middlebox/relay compatibility (RFC 8446 allows 0x0301 here); all other
    // records use 0x0303.
    g_out[2] = (type == REC_HANDSHAKE) ? 0x01 : TLS_LEGACY_VERSION_LO;
    put16(g_out + 3, static_cast<uint16_t>(len));
    for (uint32_t i = 0; i < len; i++) g_out[5 + i] = data[i];
    return net->tcpStreamSend(g_out, static_cast<uint16_t>(len + 5));
}

static void buildNonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12]) {
    for (int i = 0; i < 12; i++) nonce[i] = iv[i];
    for (int i = 0; i < 8; i++) nonce[11 - i] ^= static_cast<uint8_t>((seq >> (8 * i)) & 0xff);
}

bool TlsClient::sendEncryptedRecord(uint8_t innerType, const uint8_t* data, uint32_t len) {
    if (!haveSendKeys) return false;
    // TLSInnerPlaintext = content || inner_type (no padding)
    uint32_t ptLen = len + 1;
    uint32_t recLen = ptLen + 16; // + GCM tag
    if (recLen + 5 > sizeof(g_out)) return false;
    g_out[0] = REC_APPDATA;
    g_out[1] = TLS_LEGACY_VERSION_HI;
    g_out[2] = TLS_LEGACY_VERSION_LO;
    put16(g_out + 3, static_cast<uint16_t>(recLen));
    uint8_t aad[5];
    aad[0] = REC_APPDATA; aad[1] = TLS_LEGACY_VERSION_HI; aad[2] = TLS_LEGACY_VERSION_LO;
    put16(aad + 3, static_cast<uint16_t>(recLen));
    // plaintext into g_plain
    for (uint32_t i = 0; i < len; i++) g_plain[i] = data[i];
    g_plain[len] = innerType;
    uint8_t nonce[12]; buildNonce(sendIV, sendSeq, nonce);
    uint8_t* ct = g_out + 5;
    aes128_gcm_encrypt(sendKey, nonce, aad, 5, g_plain, ptLen, ct, ct + ptLen);
    sendSeq++;
    return net->tcpStreamSend(g_out, static_cast<uint16_t>(recLen + 5));
}

bool TlsClient::readRecord(uint8_t* typeOut, uint8_t* body, uint32_t cap, uint32_t* lenOut, uint32_t timeoutMs) {
    uint8_t hdr[5];
    if (net->tcpStreamRead(hdr, 5, timeoutMs) != 5) return false;
    uint32_t len = (static_cast<uint32_t>(hdr[3]) << 8) | hdr[4];
    if (len > cap) return false;
    uint32_t got = 0;
    while (got < len) {
        uint32_t want = len - got;
        if (want > 4096) want = 4096;
        uint16_t n = net->tcpStreamRead(body + got, static_cast<uint16_t>(want), timeoutMs);
        if (n == 0) return false;
        got += n;
    }
    *typeOut = hdr[0];
    *lenOut = len;
    return true;
}

int32_t TlsClient::decryptRecord(const uint8_t* body, uint32_t bodyLen, uint8_t* out, uint8_t* innerTypeOut) {
    if (!haveRecvKeys || bodyLen < 16 + 1) return -1;
    uint32_t ptLen = bodyLen - 16;
    uint8_t aad[5];
    aad[0] = REC_APPDATA; aad[1] = TLS_LEGACY_VERSION_HI; aad[2] = TLS_LEGACY_VERSION_LO;
    put16(aad + 3, static_cast<uint16_t>(bodyLen));
    uint8_t nonce[12]; buildNonce(recvIV, recvSeq, nonce);
    if (!aes128_gcm_decrypt(recvKey, nonce, aad, 5, body, ptLen, body + ptLen, out)) return -1;
    recvSeq++;
    // Strip zero padding, then the trailing real content type byte.
    int32_t n = static_cast<int32_t>(ptLen);
    while (n > 0 && out[n - 1] == 0) n--;
    if (n == 0) return -1;
    *innerTypeOut = out[n - 1];
    return n - 1;
}

void TlsClient::setSendKeys(const uint8_t ts[32]) {
    hkdfExpandLabel(ts, "key", nullptr, 0, sendKey, 16);
    hkdfExpandLabel(ts, "iv", nullptr, 0, sendIV, 12);
    sendSeq = 0; haveSendKeys = true;
}
void TlsClient::setRecvKeys(const uint8_t ts[32]) {
    hkdfExpandLabel(ts, "key", nullptr, 0, recvKey, 16);
    hkdfExpandLabel(ts, "iv", nullptr, 0, recvIV, 12);
    recvSeq = 0; haveRecvKeys = true;
}

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------
bool TlsClient::sendClientHello() {
    sha256_init(&g_transcript);
    rng_bytes(clientRandom, 32);
    rng_bytes(sessionId, 32);
    rng_bytes(clientPriv, 32);
    x25519_base(clientPub, clientPriv);

    uint8_t body[512];
    uint32_t p = 0;
    body[p++] = 0x03; body[p++] = 0x03; // legacy_version
    for (int i = 0; i < 32; i++) body[p++] = clientRandom[i];
    body[p++] = 32; // legacy_session_id length
    for (int i = 0; i < 32; i++) body[p++] = sessionId[i];
    // cipher_suites: TLS_AES_128_GCM_SHA256
    put16(body + p, 2); p += 2;
    put16(body + p, 0x1301); p += 2;
    // legacy_compression_methods
    body[p++] = 1; body[p++] = 0;
    // Extensions. Real Tor relays drop ClientHellos with a too-minimal extension
    // set (a scanner/DoS mitigation), so we present a normal client's profile.
    uint32_t extLenPos = p; p += 2;
    uint32_t extStart = p;
    // ec_point_formats: uncompressed
    put16(body + p, 0x000b); p += 2; put16(body + p, 2); p += 2; body[p++] = 1; body[p++] = 0;
    // supported_groups: x25519
    put16(body + p, 0x000a); p += 2; put16(body + p, 4); p += 2;
    put16(body + p, 2); p += 2; put16(body + p, 0x001d); p += 2;
    // session_ticket (empty)
    put16(body + p, 0x0023); p += 2; put16(body + p, 0); p += 2;
    // encrypt_then_mac (empty)
    put16(body + p, 0x0016); p += 2; put16(body + p, 0); p += 2;
    // extended_master_secret (empty)
    put16(body + p, 0x0017); p += 2; put16(body + p, 0); p += 2;
    // signature_algorithms (broad list; we don't verify, but the server needs one)
    {
        static const uint16_t sa[] = {
            0x0804,0x0805,0x0806, 0x0401,0x0501,0x0601, 0x0403,0x0503,0x0603,
            0x0807,0x0808, 0x0201,0x0203
        };
        uint16_t cnt = sizeof(sa) / sizeof(sa[0]);
        uint16_t listLen = static_cast<uint16_t>(cnt * 2);
        put16(body + p, 0x000d); p += 2; put16(body + p, listLen + 2); p += 2;
        put16(body + p, listLen); p += 2;
        for (uint16_t i = 0; i < cnt; i++) { put16(body + p, sa[i]); p += 2; }
    }
    // supported_versions: TLS 1.3
    put16(body + p, 0x002b); p += 2; put16(body + p, 3); p += 2;
    body[p++] = 2; put16(body + p, 0x0304); p += 2;
    // psk_key_exchange_modes: psk_dhe_ke
    put16(body + p, 0x002d); p += 2; put16(body + p, 2); p += 2; body[p++] = 1; body[p++] = 0x01;
    // key_share: x25519 client public
    put16(body + p, 0x0033); p += 2; put16(body + p, 38); p += 2;
    put16(body + p, 36); p += 2;              // client_shares length
    put16(body + p, 0x001d); p += 2;          // group x25519
    put16(body + p, 32); p += 2;              // key length
    for (int i = 0; i < 32; i++) body[p++] = clientPub[i];
    // compress_certificate (zlib, zstd). Tor relays fingerprint ClientHellos and
    // drop ones lacking this; the server may then send a CompressedCertificate,
    // which we hash opaquely into the transcript (we never validate the cert).
    put16(body + p, 0x001b); p += 2; put16(body + p, 5); p += 2;
    body[p++] = 4; put16(body + p, 0x0001); p += 2; put16(body + p, 0x0003); p += 2;
    put16(body + extLenPos, static_cast<uint16_t>(p - extStart));

    uint8_t msg[600];
    msg[0] = HS_CLIENT_HELLO;
    msg[1] = 0; msg[2] = static_cast<uint8_t>(p >> 8); msg[3] = static_cast<uint8_t>(p);
    for (uint32_t i = 0; i < p; i++) msg[4 + i] = body[i];
    uint32_t msgLen = 4 + p;
    sha256_update(&g_transcript, msg, msgLen);
    stage = "client_hello";
    return sendPlainRecord(REC_HANDSHAKE, msg, msgLen);
}

bool TlsClient::readServerHello(uint32_t timeoutMs) {
    stage = "server_hello";
    uint8_t type; uint32_t rlen;
    // Skip any leading ChangeCipherSpec (middlebox compat).
    for (int i = 0; i < 4; i++) {
        if (!readRecord(&type, g_rec, sizeof(g_rec), &rlen, timeoutMs)) { stage = "server_hello_timeout"; return false; }
        if (type == REC_CHANGE_CIPHER) continue;
        break;
    }
    if (type == REC_ALERT) { if (rlen >= 2) { alertLvl = g_rec[0]; alertDesc = g_rec[1]; } stage = "server_hello_alert"; return false; }
    if (type != REC_HANDSHAKE || rlen < 4 || g_rec[0] != HS_SERVER_HELLO) { stage = "not_server_hello"; return false; }

    uint32_t mlen = (static_cast<uint32_t>(g_rec[1]) << 16) | (static_cast<uint32_t>(g_rec[2]) << 8) | g_rec[3];
    if (4 + mlen > rlen) { stage = "server_hello_short"; return false; }
    sha256_update(&g_transcript, g_rec, 4 + mlen);
    gotServerHello = true;

    const uint8_t* b = g_rec + 4;
    uint32_t off = 2 + 32;            // legacy_version + random
    if (off >= mlen) return false;
    uint8_t sidLen = b[off++];
    off += sidLen;
    if (off + 3 > mlen) { stage = "server_hello_parse"; return false; }
    suite = static_cast<uint16_t>((b[off] << 8) | b[off + 1]); off += 2;
    off += 1;                         // legacy_compression_method
    if (off + 2 > mlen) { stage = "server_hello_noext"; return false; }
    uint32_t extTotal = static_cast<uint32_t>((b[off] << 8) | b[off + 1]); off += 2;
    uint32_t extEnd = off + extTotal;
    if (extEnd > mlen) extEnd = mlen;
    while (off + 4 <= extEnd) {
        uint16_t et = static_cast<uint16_t>((b[off] << 8) | b[off + 1]);
        uint16_t el = static_cast<uint16_t>((b[off + 2] << 8) | b[off + 3]);
        off += 4;
        if (off + el > extEnd) break;
        if (et == 0x0033 && el >= 4) {           // key_share
            group = static_cast<uint16_t>((b[off] << 8) | b[off + 1]);
            uint16_t kl = static_cast<uint16_t>((b[off + 2] << 8) | b[off + 3]);
            if (group == 0x001d && kl == 32 && el >= 4 + 32) {
                for (int i = 0; i < 32; i++) serverPub[i] = b[off + 4 + i];
                gotKeyShare = true;
            }
        }
        off += el;
    }
    return gotKeyShare && suite == 0x1301;
}

void TlsClient::deriveHandshakeKeys() {
    uint8_t ecdh[32];
    x25519_scalarmult(ecdh, clientPriv, serverPub);

    uint8_t zeros[32]; for (int i = 0; i < 32; i++) zeros[i] = 0;
    uint8_t earlySecret[32];
    // Early Secret = HKDF-Extract(salt=0, IKM=PSK). With no PSK, IKM is 32 zero
    // bytes (NOT an empty string), per RFC 8446 7.1.
    hkdf_sha256_extract(zeros, 32, zeros, 32, earlySecret);
    uint8_t derived[32];
    deriveSecret(earlySecret, "derived", SHA256_EMPTY, derived);
    hkdf_sha256_extract(derived, 32, ecdh, 32, handshakeSecret);

    uint8_t th[32]; transcriptHashNow(th); // CH || SH
    deriveSecret(handshakeSecret, "c hs traffic", th, cHsTraffic);
    deriveSecret(handshakeSecret, "s hs traffic", th, sHsTraffic);

    uint8_t derived2[32];
    deriveSecret(handshakeSecret, "derived", SHA256_EMPTY, derived2);
    hkdf_sha256_extract(derived2, 32, zeros, 32, masterSecret); // Master Secret: IKM = 0[32]

    setSendKeys(cHsTraffic);
    setRecvKeys(sHsTraffic);
}

bool TlsClient::readEncryptedFlight(uint32_t timeoutMs) {
    stage = "encrypted_flight";
    uint32_t hsLen = 0, hsPos = 0;
    uint8_t serverFinishedKey[32];
    hkdfExpandLabel(sHsTraffic, "finished", nullptr, 0, serverFinishedKey, 32);

    while (!gotServerFinished) {
        uint8_t type; uint32_t rlen;
        if (!readRecord(&type, g_rec, sizeof(g_rec), &rlen, timeoutMs)) { stage = "flight_timeout"; return false; }
        if (type == REC_CHANGE_CIPHER) continue;
        if (type == REC_ALERT) { stage = "flight_alert"; return false; } // plaintext alert
        if (type != REC_APPDATA) continue;
        uint8_t innerType;
        int32_t pl = decryptRecord(g_rec, rlen, g_plain, &innerType);
        if (pl < 0) { stage = "flight_bad_mac"; return false; }
        if (innerType == REC_ALERT) { if (pl >= 2) { alertLvl = g_plain[0]; alertDesc = g_plain[1]; } stage = "flight_enc_alert"; return false; }
        if (innerType != REC_HANDSHAKE) continue;

        // Accumulate handshake bytes and parse complete messages.
        if (hsLen + static_cast<uint32_t>(pl) > sizeof(g_hs)) {
            for (uint32_t i = hsPos; i < hsLen; i++) g_hs[i - hsPos] = g_hs[i];
            hsLen -= hsPos; hsPos = 0;
            if (hsLen + static_cast<uint32_t>(pl) > sizeof(g_hs)) { stage = "flight_overflow"; return false; }
        }
        for (int32_t i = 0; i < pl; i++) g_hs[hsLen++] = g_plain[i];

        while (hsLen - hsPos >= 4) {
            uint8_t mtype = g_hs[hsPos];
            uint32_t ml = (static_cast<uint32_t>(g_hs[hsPos + 1]) << 16) |
                          (static_cast<uint32_t>(g_hs[hsPos + 2]) << 8) | g_hs[hsPos + 3];
            if (hsLen - hsPos < 4 + ml) break;
            const uint8_t* msg = g_hs + hsPos;
            uint32_t fullLen = 4 + ml;

            if (mtype == HS_FINISHED) {
                uint8_t th[32]; transcriptHashNow(th); // CH..CertificateVerify
                uint8_t expect[32];
                hmac_sha256(serverFinishedKey, 32, th, 32, expect);
                if (ml >= 32 && crypto_equal(expect, msg + 4, 32)) gotServerFinished = true;
                else { stage = "server_finished_mismatch"; return false; }
                sha256_update(&g_transcript, msg, fullLen);
                hsPos += fullLen;
                break;
            }

            sha256_update(&g_transcript, msg, fullLen);
            if (mtype == HS_ENCRYPTED_EXTENSIONS) gotEncryptedExtensions = true;
            else if (mtype == HS_CERTIFICATE) { gotCertificate = true; certBytes = ml; }
            hsPos += fullLen;
        }
    }
    return gotServerFinished;
}

void TlsClient::deriveApplicationKeys() {
    uint8_t th[32]; transcriptHashNow(th); // CH..server Finished
    deriveSecret(masterSecret, "c ap traffic", th, cApTraffic);
    deriveSecret(masterSecret, "s ap traffic", th, sApTraffic);
}

bool TlsClient::sendClientFinished() {
    uint8_t clientFinishedKey[32];
    hkdfExpandLabel(cHsTraffic, "finished", nullptr, 0, clientFinishedKey, 32);
    uint8_t th[32]; transcriptHashNow(th); // CH..server Finished
    uint8_t verify[32];
    hmac_sha256(clientFinishedKey, 32, th, 32, verify);
    uint8_t fin[4 + 32];
    fin[0] = HS_FINISHED; fin[1] = 0; fin[2] = 0; fin[3] = 32;
    for (int i = 0; i < 32; i++) fin[4 + i] = verify[i];
    stage = "client_finished";
    // Encrypted with the client handshake traffic keys (sendSeq still 0).
    return sendEncryptedRecord(REC_HANDSHAKE, fin, sizeof(fin));
}

bool TlsClient::handshake(uint32_t timeoutMs) {
    if (!net) return false;
    if (!sendClientHello()) { stage = "client_hello_send_fail"; return false; }
    if (!readServerHello(timeoutMs)) return false;
    if (suite != 0x1301) { stage = "unexpected_suite"; return false; }
    if (!gotKeyShare) { stage = "no_server_keyshare"; return false; }
    deriveHandshakeKeys();
    // Middlebox-compat ChangeCipherSpec (harmless on a direct link).
    uint8_t ccs = 0x01;
    sendPlainRecord(REC_CHANGE_CIPHER, &ccs, 1);
    if (!readEncryptedFlight(timeoutMs)) return false;
    deriveApplicationKeys();
    if (!sendClientFinished()) { stage = "client_finished_fail"; return false; }
    // Switch to application traffic keys in both directions.
    setSendKeys(cApTraffic);
    setRecvKeys(sApTraffic);
    est = true;
    stage = "established";
    Serial::writeString("[tls] TLS 1.3 handshake established\n");
    return true;
}

// ---------------------------------------------------------------------------
// Application data
// ---------------------------------------------------------------------------
bool TlsClient::write(const uint8_t* data, uint32_t len) {
    if (!est) return false;
    uint32_t off = 0;
    while (off < len) {
        uint32_t n = len - off;
        if (n > 16384) n = 16384;
        if (!sendEncryptedRecord(REC_APPDATA, data + off, n)) return false;
        off += n;
    }
    return true;
}

uint32_t TlsClient::read(uint8_t* out, uint32_t needed, uint32_t timeoutMs) {
    if (!est) return 0;
    uint32_t done = 0;
    while (done < needed) {
        if (appPos < appLen) {
            uint32_t n = appLen - appPos;
            if (n > needed - done) n = needed - done;
            for (uint32_t i = 0; i < n; i++) out[done + i] = g_appBuf[appPos + i];
            appPos += n; done += n;
            continue;
        }
        uint8_t type; uint32_t rlen;
        if (!readRecord(&type, g_rec, sizeof(g_rec), &rlen, timeoutMs)) break;
        if (type == REC_CHANGE_CIPHER) continue;
        if (type != REC_APPDATA) { if (type == REC_ALERT) break; continue; }
        uint8_t innerType;
        int32_t pl = decryptRecord(g_rec, rlen, g_appBuf, &innerType);
        if (pl < 0) break;
        if (innerType == REC_APPDATA) { appPos = 0; appLen = static_cast<uint32_t>(pl); }
        else if (innerType == REC_ALERT) { if (pl >= 2) { alertLvl = g_appBuf[0]; alertDesc = g_appBuf[1]; } break; }
        // innerType == handshake (e.g. NewSessionTicket): ignore, read next record
    }
    return done;
}
