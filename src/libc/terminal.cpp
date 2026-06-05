#include <terminal.hpp>
#include <string.hpp>
#include <interrupts.hpp>
#include <serial.hpp>
#include <memory.hpp>
#include <crypto.hpp>

static const char* TERMINAL_HEX = "0123456789ABCDEF";

static void u32ToDec(uint32_t value, char* out, int outLen) {
    if (!out || outLen <= 0) {
        return;
    }
    if (value == 0) {
        if (outLen > 1) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';
        }
        return;
    }

    char tmp[11];
    int i = 0;
    while (value > 0 && i < 10) {
        tmp[i++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    int p = 0;
    while (i > 0 && p < outLen - 1) {
        out[p++] = tmp[--i];
    }
    out[p] = '\0';
}

static void u32ToHex(uint32_t value, char* out, int outLen) {
    if (!out || outLen < 11) {
        return;
    }
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; i++) {
        out[2 + i] = TERMINAL_HEX[(value >> ((7 - i) * 4)) & 0xF];
    }
    out[10] = '\0';
}

static bool textContains(const char* haystack, const char* needle) {
    if (!haystack || !needle || needle[0] == '\0') {
        return false;
    }
    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (needle[j] && haystack[i + j] == needle[j]) {
            j++;
        }
        if (needle[j] == '\0') {
            return true;
        }
    }
    return false;
}

struct TorConsensusSummary {
    uint32_t relays;
    uint32_t guards;
    uint32_t exits;
    uint32_t fast;
    uint32_t stable;
    uint32_t running;
    uint32_t valid;
    uint32_t usableGuards;
    char selectedNickname[32];
    char selectedIp[16];
    uint32_t selectedOrPort;
    bool selectedFast;
    bool selectedStable;
    bool selectedRunning;
    bool selectedValid;
    bool selectedExit;
};

struct TorRelayCandidate {
    char nickname[32];
    char ip[16];
    uint32_t orPort;
};

static bool lineStartsWith(const char* line, const char* prefix) {
    if (!line || !prefix) return false;
    for (int i = 0; prefix[i]; i++) {
        if (line[i] != prefix[i]) return false;
    }
    return true;
}

static bool flagInLine(const char* line, const char* end, const char* flag) {
    if (!line || !end || !flag) return false;
    const char* p = line;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        const char* start = p;
        int k = 0;
        while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            if (flag[k] && *p == flag[k]) k++;
            else k = -1000;
            p++;
        }
        if (k >= 0 && flag[k] == '\0' && start < p) return true;
    }
    return false;
}

static const char* skipField(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
    return p;
}

static const char* copyField(const char* p, const char* end, char* out, int outLen) {
    if (!out || outLen <= 0) return p;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    int o = 0;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (o < outLen - 1) out[o++] = *p;
        p++;
    }
    out[o] = '\0';
    return p;
}

static const char* parseU32Field(const char* p, const char* end, uint32_t* out) {
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    uint32_t value = 0;
    bool any = false;
    while (p < end && *p >= '0' && *p <= '9') {
        any = true;
        value = value * 10u + static_cast<uint32_t>(*p - '0');
        p++;
    }
    if (out) *out = any ? value : 0;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
    return p;
}

static bool parseTorRLine(const char* line, const char* end, TorRelayCandidate* out) {
    if (!lineStartsWith(line, "r ") || !out) return false;
    out->nickname[0] = '\0';
    out->ip[0] = '\0';
    out->orPort = 0;
    const char* p = line + 2;
    p = copyField(p, end, out->nickname, sizeof(out->nickname));
    // identity, digest, publication date, publication time
    p = skipField(p, end);
    p = skipField(p, end);
    p = skipField(p, end);
    p = skipField(p, end);
    p = copyField(p, end, out->ip, sizeof(out->ip));
    p = parseU32Field(p, end, &out->orPort);
    return out->nickname[0] != '\0' && out->ip[0] != '\0' && out->orPort != 0;
}

static void clearTorConsensusSummary(TorConsensusSummary* s) {
    if (!s) return;
    s->relays = 0;
    s->guards = 0;
    s->exits = 0;
    s->fast = 0;
    s->stable = 0;
    s->running = 0;
    s->valid = 0;
    s->usableGuards = 0;
    s->selectedNickname[0] = '\0';
    s->selectedIp[0] = '\0';
    s->selectedOrPort = 0;
    s->selectedFast = false;
    s->selectedStable = false;
    s->selectedRunning = false;
    s->selectedValid = false;
    s->selectedExit = false;
}

static void parseTorConsensus(const char* text, TorConsensusSummary* summary) {
    clearTorConsensusSummary(summary);
    if (!text || !summary) return;

    TorRelayCandidate pending;
    pending.nickname[0] = '\0';
    pending.ip[0] = '\0';
    pending.orPort = 0;
    bool havePending = false;

    const char* line = text;
    while (*line) {
        const char* end = line;
        while (*end && *end != '\n' && *end != '\r') end++;

        if (lineStartsWith(line, "r ")) {
            if (parseTorRLine(line, end, &pending)) {
                summary->relays++;
                havePending = true;
            } else {
                havePending = false;
            }
        } else if (lineStartsWith(line, "s ")) {
            bool hasGuard = flagInLine(line + 2, end, "Guard");
            bool hasExit = flagInLine(line + 2, end, "Exit");
            bool hasFast = flagInLine(line + 2, end, "Fast");
            bool hasStable = flagInLine(line + 2, end, "Stable");
            bool hasRunning = flagInLine(line + 2, end, "Running");
            bool hasValid = flagInLine(line + 2, end, "Valid");
            if (hasGuard) summary->guards++;
            if (hasExit) summary->exits++;
            if (hasFast) summary->fast++;
            if (hasStable) summary->stable++;
            if (hasRunning) summary->running++;
            if (hasValid) summary->valid++;
            bool usable = havePending && hasGuard && hasFast && hasStable && hasRunning && hasValid;
            if (usable) {
                summary->usableGuards++;
                if (summary->selectedNickname[0] == '\0') {
                    int i = 0;
                    for (; pending.nickname[i] && i < 31; i++) summary->selectedNickname[i] = pending.nickname[i];
                    summary->selectedNickname[i] = '\0';
                    i = 0;
                    for (; pending.ip[i] && i < 15; i++) summary->selectedIp[i] = pending.ip[i];
                    summary->selectedIp[i] = '\0';
                    summary->selectedOrPort = pending.orPort;
                    summary->selectedFast = hasFast;
                    summary->selectedStable = hasStable;
                    summary->selectedRunning = hasRunning;
                    summary->selectedValid = hasValid;
                    summary->selectedExit = hasExit;
                }
            }
        }

        line = end;
        while (*line == '\n' || *line == '\r') line++;
    }
}

static const uint8_t TOR_TLS_CLIENT_HELLO[] = {
    // OpenSSL-style TLS 1.2 ClientHello accepted by Tor ORPorts.
    // This is only a transport probe; full native Tor still needs key schedule and encrypted records.
    0x16, 0x03, 0x01, 0x01, 0x36, 0x01, 0x00, 0x01, 0x32, 0x03, 0x03, 0xe5,
    0x37, 0x62, 0x49, 0x0c, 0x17, 0x9d, 0x9e, 0xa8, 0x87, 0x50, 0x79, 0xb6,
    0x53, 0x31, 0x20, 0xe0, 0x81, 0xc4, 0x63, 0xba, 0x60, 0xb9, 0x56, 0x7c,
    0x97, 0x2f, 0xe8, 0xa8, 0x4c, 0x06, 0xbc, 0x00, 0x00, 0xae, 0xc0, 0x2c,
    0xc0, 0x30, 0x00, 0xa3, 0x00, 0x9f, 0xcc, 0xa9, 0xcc, 0xa8, 0xcc, 0xaa,
    0xc0, 0xad, 0xc0, 0x9f, 0xc0, 0x5d, 0xc0, 0x61, 0xc0, 0x57, 0xc0, 0x53,
    0x00, 0xa7, 0xc0, 0x2b, 0xc0, 0x2f, 0x00, 0xa2, 0x00, 0x9e, 0xc0, 0xac,
    0xc0, 0x9e, 0xc0, 0x5c, 0xc0, 0x60, 0xc0, 0x56, 0xc0, 0x52, 0x00, 0xa6,
    0xc0, 0xaf, 0xc0, 0xae, 0xc0, 0xa3, 0xc0, 0xa2, 0xc0, 0x24, 0xc0, 0x28,
    0x00, 0x6b, 0x00, 0x6a, 0xc0, 0x73, 0xc0, 0x77, 0x00, 0xc4, 0x00, 0xc3,
    0x00, 0x6d, 0x00, 0xc5, 0xc0, 0x23, 0xc0, 0x27, 0x00, 0x67, 0x00, 0x40,
    0xc0, 0x72, 0xc0, 0x76, 0x00, 0xbe, 0x00, 0xbd, 0x00, 0x6c, 0x00, 0xbf,
    0xc0, 0x0a, 0xc0, 0x14, 0x00, 0x39, 0x00, 0x38, 0x00, 0x88, 0x00, 0x87,
    0xc0, 0x19, 0x00, 0x3a, 0x00, 0x89, 0xc0, 0x09, 0xc0, 0x13, 0x00, 0x33,
    0x00, 0x32, 0x00, 0x9a, 0x00, 0x99, 0x00, 0x45, 0x00, 0x44, 0xc0, 0x18,
    0x00, 0x34, 0x00, 0x9b, 0x00, 0x46, 0x00, 0x9d, 0xc0, 0x9d, 0xc0, 0x51,
    0x00, 0x9c, 0xc0, 0x9c, 0xc0, 0x50, 0xc0, 0xa1, 0xc0, 0xa0, 0x00, 0x3d,
    0x00, 0xc0, 0x00, 0x3c, 0x00, 0xba, 0x00, 0x35, 0x00, 0x84, 0x00, 0x2f,
    0x00, 0x96, 0x00, 0x41, 0x01, 0x00, 0x00, 0x5b, 0xff, 0x01, 0x00, 0x01,
    0x00, 0x00, 0x0b, 0x00, 0x02, 0x01, 0x00, 0x00, 0x0a, 0x00, 0x0c, 0x00,
    0x0a, 0x00, 0x1d, 0x00, 0x17, 0x00, 0x1e, 0x00, 0x18, 0x00, 0x19, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,
    0x0d, 0x00, 0x30, 0x00, 0x2e, 0x04, 0x03, 0x05, 0x03, 0x06, 0x03, 0x08,
    0x07, 0x08, 0x08, 0x08, 0x09, 0x08, 0x0a, 0x08, 0x0b, 0x08, 0x04, 0x08,
    0x05, 0x08, 0x06, 0x04, 0x01, 0x05, 0x01, 0x06, 0x01, 0x03, 0x03, 0x02,
    0x03, 0x03, 0x01, 0x02, 0x01, 0x03, 0x02, 0x02, 0x02, 0x04, 0x02, 0x05,
    0x02, 0x06, 0x02
};

static void byteToHex(uint8_t value, char* out) {
    out[0] = TERMINAL_HEX[(value >> 4) & 0x0f];
    out[1] = TERMINAL_HEX[value & 0x0f];
    out[2] = '\0';
}

struct TlsParsedRecord {
    bool validRecord;
    bool isHandshake;
    bool isAlert;
    uint8_t recordType;
    uint8_t major;
    uint8_t minor;
    uint16_t recordLen;
    uint8_t handshakeType;
    uint32_t handshakeLen;
    uint8_t alertLevel;
    uint8_t alertDescription;
};

static void clearTlsParsedRecord(TlsParsedRecord* rec) {
    if (!rec) return;
    rec->validRecord = false;
    rec->isHandshake = false;
    rec->isAlert = false;
    rec->recordType = 0;
    rec->major = 0;
    rec->minor = 0;
    rec->recordLen = 0;
    rec->handshakeType = 0;
    rec->handshakeLen = 0;
    rec->alertLevel = 0;
    rec->alertDescription = 0;
}

static bool parseTlsRecord(const uint8_t* data, uint16_t len, TlsParsedRecord* rec) {
    clearTlsParsedRecord(rec);
    if (!data || !rec || len < 5) return false;
    rec->recordType = data[0];
    rec->major = data[1];
    rec->minor = data[2];
    rec->recordLen = static_cast<uint16_t>((static_cast<uint16_t>(data[3]) << 8) | data[4]);
    rec->validRecord = (rec->major == 0x03) &&
        (rec->recordType == 0x16 || rec->recordType == 0x15 || rec->recordType == 0x14 || rec->recordType == 0x17) &&
        (rec->recordLen <= 16384 + 2048);
    if (!rec->validRecord) return false;
    if (rec->recordType == 0x16 && len >= 9) {
        rec->isHandshake = true;
        rec->handshakeType = data[5];
        rec->handshakeLen = (static_cast<uint32_t>(data[6]) << 16) |
            (static_cast<uint32_t>(data[7]) << 8) | static_cast<uint32_t>(data[8]);
    } else if (rec->recordType == 0x15 && len >= 7) {
        rec->isAlert = true;
        rec->alertLevel = data[5];
        rec->alertDescription = data[6];
    }
    return true;
}

static const char* tlsRecordTypeName(uint8_t t) {
    if (t == 0x14) return "ChangeCipherSpec";
    if (t == 0x15) return "Alert";
    if (t == 0x16) return "Handshake";
    if (t == 0x17) return "ApplicationData";
    return "Unknown";
}

static const char* tlsHandshakeTypeName(uint8_t t) {
    if (t == 0x01) return "ClientHello";
    if (t == 0x02) return "ServerHello";
    if (t == 0x0b) return "Certificate";
    if (t == 0x0c) return "ServerKeyExchange";
    if (t == 0x0d) return "CertificateRequest";
    if (t == 0x0e) return "ServerHelloDone";
    if (t == 0x10) return "ClientKeyExchange";
    if (t == 0x14) return "Finished";
    return "Unknown";
}

static const char* tlsAlertDescriptionName(uint8_t d) {
    if (d == 0) return "close_notify";
    if (d == 10) return "unexpected_message";
    if (d == 40) return "handshake_failure";
    if (d == 47) return "illegal_parameter";
    if (d == 70) return "protocol_version";
    if (d == 80) return "internal_error";
    if (d == 109) return "missing_extension";
    if (d == 112) return "unrecognized_name";
    return "unknown";
}


struct TlsHandshakeScan {
    uint16_t records;
    uint16_t handshakes;
    bool serverHello;
    bool certificate;
    bool serverKeyExchange;
    bool serverHelloDone;
    bool changeCipherSpec;
    bool applicationData;
    bool alert;
    uint8_t lastRecordType;
    uint8_t lastHandshakeType;
    uint16_t lastRecordLen;
    uint16_t firstCipherSuite;
    uint16_t selectedCurve;
};

static void clearTlsHandshakeScan(TlsHandshakeScan* scan) {
    if (!scan) return;
    scan->records = 0;
    scan->handshakes = 0;
    scan->serverHello = false;
    scan->certificate = false;
    scan->serverKeyExchange = false;
    scan->serverHelloDone = false;
    scan->changeCipherSpec = false;
    scan->applicationData = false;
    scan->alert = false;
    scan->lastRecordType = 0;
    scan->lastHandshakeType = 0;
    scan->lastRecordLen = 0;
    scan->firstCipherSuite = 0;
    scan->selectedCurve = 0;
}

static void scanTlsHandshakeRecords(const uint8_t* data, uint16_t len, TlsHandshakeScan* scan) {
    clearTlsHandshakeScan(scan);
    if (!data || !scan) return;
    uint16_t off = 0;
    while (off + 5 <= len) {
        uint8_t typ = data[off];
        uint16_t recLen = static_cast<uint16_t>((static_cast<uint16_t>(data[off + 3]) << 8) | data[off + 4]);
        if (data[off + 1] != 0x03 || recLen > 18432 || off + 5u + recLen > len) break;
        scan->records++;
        scan->lastRecordType = typ;
        scan->lastRecordLen = recLen;
        if (typ == 0x14) scan->changeCipherSpec = true;
        else if (typ == 0x15) scan->alert = true;
        else if (typ == 0x17) scan->applicationData = true;
        else if (typ == 0x16) {
            uint16_t pos = static_cast<uint16_t>(off + 5);
            uint16_t end = static_cast<uint16_t>(pos + recLen);
            while (pos + 4 <= end) {
                uint8_t htype = data[pos];
                uint32_t hlen = (static_cast<uint32_t>(data[pos + 1]) << 16) |
                                (static_cast<uint32_t>(data[pos + 2]) << 8) |
                                static_cast<uint32_t>(data[pos + 3]);
                if (pos + 4u + hlen > end) break;
                scan->handshakes++;
                scan->lastHandshakeType = htype;
                if (htype == 0x02) {
                    scan->serverHello = true;
                    const uint8_t* sh = data + pos + 4;
                    if (hlen >= 38) {
                        uint16_t sidLen = sh[34];
                        uint32_t csOff = 35u + sidLen;
                        if (csOff + 1 < hlen) {
                            scan->firstCipherSuite = static_cast<uint16_t>((static_cast<uint16_t>(sh[csOff]) << 8) | sh[csOff + 1]);
                        }
                    }
                } else if (htype == 0x0b) scan->certificate = true;
                else if (htype == 0x0c) {
                    scan->serverKeyExchange = true;
                    const uint8_t* sk = data + pos + 4;
                    if (hlen >= 4 && sk[0] == 0x03) {
                        scan->selectedCurve = static_cast<uint16_t>((static_cast<uint16_t>(sk[1]) << 8) | sk[2]);
                    }
                } else if (htype == 0x0e) scan->serverHelloDone = true;
                pos = static_cast<uint16_t>(pos + 4u + hlen);
            }
        }
        off = static_cast<uint16_t>(off + 5u + recLen);
    }
}

static void printHex16ToBuf(uint16_t v, char* out) {
    out[0] = '0'; out[1] = 'x';
    out[2] = TERMINAL_HEX[(v >> 12) & 0xF];
    out[3] = TERMINAL_HEX[(v >> 8) & 0xF];
    out[4] = TERMINAL_HEX[(v >> 4) & 0xF];
    out[5] = TERMINAL_HEX[v & 0xF];
    out[6] = '\0';
}

static const char* tlsCipherSuiteName(uint16_t cs) {
    if (cs == 0xC013) return "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA";
    if (cs == 0xC014) return "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA";
    if (cs == 0xC02F) return "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
    if (cs == 0xC030) return "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
    if (cs == 0x009C) return "TLS_RSA_WITH_AES_128_GCM_SHA256";
    if (cs == 0x002F) return "TLS_RSA_WITH_AES_128_CBC_SHA";
    return "unknown";
}

static const char* tlsCurveName(uint16_t curve) {
    if (curve == 0x001D) return "x25519";
    if (curve == 0x0017) return "secp256r1";
    if (curve == 0x0018) return "secp384r1";
    return "unknown";
}


// Global terminal instance for keyboard handler
Terminal* g_terminal = nullptr;

// Keyboard handler function that's registered with the interrupt system
void terminal_keyboard_handler() {
    if (g_terminal) {
        Serial::writeString("[terminal] keyboard callback\n");
        g_terminal->handleKeypress();
    } else {
        Serial::writeString("[terminal] keyboard callback missing terminal\n");
    }
}

Terminal::Terminal() {
    inputPosition = 0;
    readingInput = false;
    commandReady = false;
    network = nullptr;
    lastDhcpState = 0;
    torDirectoryReachable = false;
    torCircuitsReady = false;
    torTlsReady = false;
    torTlsHandshakeSeen = false;
    torTlsRxLen = 0;
    torTlsRecordType = 0;
    torTlsMajor = 0;
    torTlsMinor = 0;
    torTlsRecordLen = 0;
    torTlsHandshakeType = 0;
    torTlsHandshakeLen = 0;
    torTlsAlertLevel = 0;
    torTlsAlertDescription = 0;
    torRelayCount = 0;
    torGuardCount = 0;
    torExitCount = 0;
    torFastCount = 0;
    torStableCount = 0;
    torRunningCount = 0;
    torValidCount = 0;
    torUsableGuardCount = 0;
    torSelectedNickname[0] = '\0';
    torSelectedIp[0] = '\0';
    torSelectedOrPort = 0;
    torSelectedHasFast = false;
    torSelectedHasStable = false;
    torSelectedHasRunning = false;
    torSelectedHasValid = false;
    torSelectedHasExit = false;

    // Initialize input buffer without relying on hosted-library memset.
    volatile char* buf = inputBuffer;
    for (int i = 0; i < 256; i++) {
        buf[i] = 0;
    }

    g_terminal = this;
}

void Terminal::init(Vga* vga, Interrupts* interrupts, FileSystem* filesystem, Network* network) {
    this->vga = vga;
    this->interrupts = interrupts;
    this->filesystem = filesystem;
    this->network = network;
    interrupts->registerHandler(1, terminal_keyboard_handler);
    vga->clear();
}

void Terminal::run() {
    readingInput = true;
    putString("Welcome MrHakOs Terminal\n");
    putString("Network: type dhcp when the cable/router link is ready.\n");
    showPrompt();
    
    // Wait for keyboard interrupts. Also poll the i8042 data port on every
    // timer wake so USB keyboards exposed through firmware USB Legacy Support
    // still work on real hardware when IRQ1 is not delivered after GRUB.
    while (true) {
        pollKeyboard();

        if (network) {
            network->tickDhcp();
            uint8_t dhcpState = network->getDhcpState();
            if (dhcpState != lastDhcpState) {
                lastDhcpState = dhcpState;
                if (dhcpState == 3) {
                    const NetworkInfo& info = network->getInfo();
                    static char ip[16];
                    putString("\nDHCP configured\n");
                    putString("  IP: "); network->formatIp(info.ipAddress, ip, sizeof(ip)); putString(ip); putString("\n");
                    putString("  Gateway: "); network->formatIp(info.gatewayIp, ip, sizeof(ip)); putString(ip); putString("\n");
                    putString("  DNS: "); network->formatIp(info.dnsIp, ip, sizeof(ip)); putString(ip); putString("\n");
                    putString("  Netmask: "); network->formatIp(info.netmask, ip, sizeof(ip)); putString(ip); putString("\n");
                    showPrompt();
                } else if (dhcpState == 4) {
                    putString("\nDHCP failed or timed out; network remains unconfigured\n");
                    showPrompt();
                }
            }
        }

        // Process command outside interrupt context to avoid long ISR work
        if (commandReady) {
            Serial::writeString("[terminal] commandReady in loop\n");
            commandReady = false;
            inputBuffer[inputPosition] = '\0';
            processCommand(inputBuffer);
            inputPosition = 0;
            inputBuffer[0] = '\0';
            showPrompt();
        }
        // Do not rely solely on HLT here. Some real GRUB/USB-legacy boots do
        // not deliver IRQ1, and a few machines also do not route the PIT/PIC as
        // expected. A tiny pause loop keeps polling responsive for USB/laptop
        // keyboards while avoiding a completely tight spin.
        for (volatile int spin = 0; spin < 10000; ++spin) {
            asm volatile("pause");
        }
    }
}

void Terminal::putChar(char c) {
    vga->putChar(c);
}

void Terminal::putString(const char* str) {
    vga->set_cursor_enabled(false);
    for (int i = 0; str[i]; i++) {
        putChar(str[i]);
    }
    vga->set_cursor_enabled(true);
    vga->force_update_cursor();
}

void Terminal::handleKeypress() {
    char key = getLastKey();
    
    if (key == 0) {
        Serial::writeString("[terminal] key zero\n");
        return;
    }
    Serial::writeString(readingInput ? "[terminal] reading key=0x" : "[terminal] not reading key=0x");
    Serial::writeHex8(static_cast<uint8_t>(key));
    Serial::writeString(" pos=0x");
    Serial::writeHex32(static_cast<uint32_t>(inputPosition));
    Serial::writeString("\n");
    
    if (readingInput) {
        if (inputPosition < 0 || inputPosition >= 255) {
            Serial::writeString("[terminal] input position repaired\n");
            inputPosition = 0;
            inputBuffer[0] = '\0';
        }
        if (key == '\n' || key == '\r') {
            commandReady = true;
        } else if (key == '\b') {
            if (inputPosition > 0) {
                inputPosition--;
                inputBuffer[inputPosition] = 0;
                int cx = vga->get_x();
                int cy = vga->get_y();
                if (cx > 0) {
                    cx--;
                } else if (cy > 0) {
                    cy--;
                    cx = 79; // move to last column of previous line
                } else {
                    // Top-left corner: nothing to erase
                }
                // Erase character visually and move cursor
                vga->putCharAt(' ', cx, cy);
                vga->set_xy(cx, cy);
            }
        } else if (inputPosition < 255) { // Regular character
            inputBuffer[inputPosition] = key;
            inputPosition++;
            
            // Echo the character
            putChar(key);
        }
    }
}

void Terminal::processCommand(const char* cmd) {
    // Use static buffers instead of stack arrays to avoid 64-bit stack issues
    // Static variables are zero-initialized by default in .bss section
    static char command[256];
    static char args[256];
    
    // Clear buffers for reuse
    command[0] = '\0';
    args[0] = '\0';
    
    // Find the first space to separate command from arguments
    int i = 0;
    while (cmd[i] != ' ' && cmd[i] != '\0') {
        command[i] = cmd[i];
        i++;
    }
    command[i] = '\0';
    
    
    // Extract arguments if any
    if (cmd[i] == ' ') {
        int j = 0;
        i++; // Skip the space
        while (cmd[i] != '\0') {
            args[j] = cmd[i];
            i++;
            j++;
        }
        args[j] = '\0';
    }

    // Process commands
    if (strcmp(command, "clear") == 0) {
        vga->clear();
        putString("Welcome MrHakOs Terminal\n");
    } else if (strcmp(command, "help") == 0) {
        putString("\n");
        putString("Available commands:\n");
        putString("  clear   - Clear the screen\n");
        putString("  help    - Show this help message\n");
        putString("  mrhakos - Show MrHakOs information\n");
        putString("  kbd     - Show keyboard IRQ/poll counters for USB legacy debugging\n");
        putString("  meminfo - Show RAM-only memory protection status\n");
        putString("  mkdir   - Create a new directory\n");
        putString("  ls      - List files and directories\n");
        putString("  cd      - Change current directory\n");
        putString("  touch   - Create a new .hak file\n");
        putString("  cat     - Display the content of a .hak file\n");
        putString("  echo    - Display text or write to a .hak file using > redirection\n");
        putString("  cp      - Copy a file\n");
        putString("  mv      - Move a file or directory\n");
        putString("  netinfo - Show detected Ethernet/PCI network state\n");
        putString("  netpoll - Poll RTL8139 receive queue once\n");
        putString("  arping  - Resolve an IPv4 address with ARP, example: arping 10.0.2.2\n");
        putString("  ping    - Linux-like ICMP echo, example: ping example.com\n");
        putString("  dhcp    - Auto-configure IP/gateway/DNS with DHCP\n");
        putString("  traceroute - Show ICMP route hops, example: traceroute example.com\n");
        putString("  udp     - Send UDP text to port 9001, example: udp 10.0.2.2 hello\n");
        putString("            Hostnames work too after DNS, example: udp myserver.com hello\n");
        putString("  dns     - Resolve a hostname with QEMU DNS, example: dns example.com\n");
        putString("  tcp     - Send TCP text, example: tcp 10.0.2.2 8080 hello\n");
        putString("  http    - Fetch / over TCP port 80, example: http example.com\n");
        putString("  curl    - HTTP client: curl [-X METHOD] http://host[:port]/path [body]\n");
        putString("            Methods: GET POST PUT PATCH DELETE HEAD OPTIONS TRACE CONNECT\n");
        putString("  socks5  - Tunnel TCP through an external SOCKS5 proxy (RFC 1928)\n");
        putString("            example: socks5 10.0.2.2 9050 example.com 80 http\n");
        putString("  tor     - Native Tor bootstrap: consensus + OR TLS probe + circuit gate\n");
        putString("            stages: tor status | tor consensus | tor tls | tor crypto | tor circuit\n");
        putString("            proxy test: tor proxy [proxyIp proxyPort]\n");
        putString("  securechat - Onion-only secure chat control, example: securechat status\n");
    } else if (strcmp(command, "mrhakos") == 0) {
        putString("\n");
        putString("Name: MrHakOS\n");
        putString("Author : Mohamed Hakkou\n");
        putString("Version : 5.0\n");
        putString("Description : A simple operating system using C++\n");
    } else if (strcmp(command, "kbd") == 0) {
        char value[16];
        putString("\nKeyboard input counters:\n");
        u32ToDec(keyboardIrqScancodes(), value, sizeof(value));
        putString("  IRQ1 scancodes: ");
        putString(value);
        putString("\n");
        u32ToDec(keyboardPolledScancodes(), value, sizeof(value));
        putString("  Polled scancodes: ");
        putString(value);
        putString("\n");
        putString("If a USB keyboard works here, firmware USB Legacy Support is translating it.\n");
    } else if (strcmp(command, "meminfo") == 0 || strcmp(command, "memory") == 0) {
        cmdMeminfo(args);
    } else if (strcmp(command, "mkdir") == 0) {
        cmdMkdir(args);
    } else if (strcmp(command, "ls") == 0) {
        cmdLs(args);
    } else if (strcmp(command, "cd") == 0) {
        cmdCd(args);
    } else if (strcmp(command, "cp") == 0) {
        cmdCp(args);
    } else if (strcmp(command, "mv") == 0) {
        cmdMv(args);
    } else if (strcmp(command, "touch") == 0) {
        cmdTouch(args);
    } else if (strcmp(command, "cat") == 0) {
        cmdCat(args);
    } else if (strcmp(command, "echo") == 0) {
        cmdEcho(args);
    } else if (strcmp(command, "netinfo") == 0) {
        cmdNetinfo(args);
    } else if (strcmp(command, "netpoll") == 0) {
        cmdNetpoll(args);
    } else if (strcmp(command, "arping") == 0) {
        cmdArping(args);
    } else if (strcmp(command, "ping") == 0) {
        cmdPing(args);
    } else if (strcmp(command, "dhcp") == 0) {
        cmdDhcp(args);
    } else if (strcmp(command, "traceroute") == 0 || strcmp(command, "trace") == 0) {
        cmdTraceroute(args);
    } else if (strcmp(command, "udp") == 0) {
        cmdUdp(args);
    } else if (strcmp(command, "dns") == 0) {
        cmdDns(args);
    } else if (strcmp(command, "tcp") == 0) {
        cmdTcp(args);
    } else if (strcmp(command, "http") == 0) {
        cmdHttp(args);
    } else if (strcmp(command, "curl") == 0) {
        cmdCurl(args);
    } else if (strcmp(command, "socks5") == 0) {
        cmdSocks5(args);
    } else if (strcmp(command, "tor") == 0) {
        cmdTor(args);
    } else if (strcmp(command, "securechat") == 0) {
        cmdSecureChat(args);
    } else if (strcmp(command, "") == 0){
        putString("\n");
    } else if (command[0] != '\0') {
        putString("\nUnknown command: ");
        putString(command);
        putString("\n");
    }
}


void Terminal::showPrompt() {
    // Show prompt
    const char* prompt = "MrHakOS >> ";
    putString(prompt);
}

void Terminal::onKeypress() {
    handleKeypress();
}

// Command handler methods
void Terminal::cmdMeminfo(const char* args) {
    (void)args;
    const KernelMemoryStatus& mem = getKernelMemoryStatus();
    static char hex[16];
    putString("\nMemory protection / RAM-only status\n");
    putString("  Storage policy: RAM-only tmpfs-style filesystem; no disk persistence by default\n");
    putString("  Paging: "); putString(mem.pagingActive ? "active\n" : "not active in this build path\n");
    putString("  CR0.WP supervisor write-protect: "); putString(mem.writeProtectEnabled ? "enabled\n" : "not enabled\n");
    putString("  NX support: "); putString(mem.nxSupported ? "yes\n" : "no/unknown\n");
    putString("  NX enabled: "); putString(mem.nxEnabled ? "yes\n" : "no\n");
    putString("  Kernel W^X policy: "); putString(mem.kernelWxProtected ? "installed\n" : "not installed yet\n");
    u32ToHex(static_cast<uint32_t>(mem.textStart), hex, sizeof(hex)); putString("  text:   "); putString(hex); putString("-");
    u32ToHex(static_cast<uint32_t>(mem.textEnd), hex, sizeof(hex)); putString(hex); putString(" RX\n");
    u32ToHex(static_cast<uint32_t>(mem.rodataStart), hex, sizeof(hex)); putString("  rodata: "); putString(hex); putString("-");
    u32ToHex(static_cast<uint32_t>(mem.rodataEnd), hex, sizeof(hex)); putString(hex); putString(" R/NX\n");
    u32ToHex(static_cast<uint32_t>(mem.dataStart), hex, sizeof(hex)); putString("  data:   "); putString(hex); putString("-");
    u32ToHex(static_cast<uint32_t>(mem.bssEnd), hex, sizeof(hex)); putString(hex); putString(" RW/NX\n");
    putString("  Next memory milestones: physical page allocator, kmalloc/kfree heap, user/kernel isolation\n");
}

void Terminal::cmdMkdir(const char* args) {
    if (args[0] == '\0') {
        putString("\nError: mkdir requires a directory name\n");
        return;
    }
    
    if (filesystem->mkdir(args)) {
        putString("\nDirectory created: ");
        putString(args);
        putString("\n");
    } else {
        putString("\nError: Could not create directory\n");
    }
}

void Terminal::cmdLs(const char* args) {
    // Unused parameter
    (void)args;
    putString("\n");
    
    // Get current directory
    FileSystemEntry* currentDir = filesystem->getCurrentDirectory();
    
    // Display current path
    char path[256];
    filesystem->getCurrentPath(path, 256);
    putString("Contents of ");
    putString(path);
    putString(":\n");
    
    // Check if directory is empty
    if (currentDir->childCount == 0) {
        putString("  <empty>\n");
        return;
    }
    
    // List all entries in current directory
    for (size_t i = 0; i < currentDir->childCount; i++) {
        FileSystemEntry* entry = currentDir->children[i];
        
        putString("  ");
        
        // Print entry name
        putString(entry->name);
        
        // Add trailing slash for directories
        if (entry->type == FS_TYPE_DIRECTORY) {
            putChar('/');
        }
        
        putString("\n");
    }
}

void Terminal::cmdCd(const char* args) {
    if (args[0] == '\0') {
        // No arguments, show current path
        char path[256];
        filesystem->getCurrentPath(path, 256);
        putString("\nCurrent directory: ");
        putString(path);
        putString("\n");
        return;
    }
    
    if (filesystem->cd(args)) {
        // Success, show new path
        char path[256];
        filesystem->getCurrentPath(path, 256);
        putString("\nChanged to: ");
        putString(path);
        putString("\n");
    } else {
        putString("\nError: Could not change directory\n");
    }
}

void Terminal::cmdCp(const char* args) {
    // Use static buffers instead of stack arrays to avoid 64-bit stack overflow
    static char source[256];
    static char destination[256];
    
    // Clear buffers
    source[0] = '\0';
    destination[0] = '\0';
    
    // Find the first space to separate source from destination
    int i = 0;
    while (args[i] != ' ' && args[i] != '\0') {
        source[i] = args[i];
        i++;
    }
    source[i] = '\0';
    
    // Extract destination if any
    if (args[i] == ' ') {
        int j = 0;
        i++; // Skip the space
        while (args[i] != '\0') {
            destination[j] = args[i];
            i++;
            j++;
        }
        destination[j] = '\0';
    }
    
    if (source[0] == '\0' || destination[0] == '\0') {
        putString("\nError: cp requires source and destination\n");
        return;
    }
    
    if (filesystem->cp(source, destination)) {
        putString("\nFile copied: ");
        putString(source);
        putString(" -> ");
        putString(destination);
        putString("\n");
    } else {
        putString("\nError: Could not copy file. Make sure source exists and destination doesn't, and both are .hak files.\n");
    }
}

void Terminal::cmdMv(const char* args) {
    // Use static buffers instead of stack arrays to avoid 64-bit stack overflow
    static char source[256];
    static char destination[256];
    
    // Clear buffers
    source[0] = '\0';
    destination[0] = '\0';
    
    // Find the first space to separate source from destination
    int i = 0;
    while (args[i] != ' ' && args[i] != '\0') {
        source[i] = args[i];
        i++;
    }
    source[i] = '\0';
    
    // Extract destination if any
    if (args[i] == ' ') {
        int j = 0;
        i++; // Skip the space
        while (args[i] != '\0') {
            destination[j] = args[i];
            i++;
            j++;
        }
        destination[j] = '\0';
    }
    
    if (source[0] == '\0' || destination[0] == '\0') {
        putString("\nError: mv requires source and destination\n");
        return;
    }
    
    if (filesystem->mv(source, destination)) {
        putString("\nMoved/renamed: ");
        putString(source);
        putString(" -> ");
        putString(destination);
        putString("\n");
    } else {
        putString("\nError: Could not move/rename. Make sure source exists, destination doesn't, and .hak files keep their extension.\n");
    }
}

void Terminal::cmdTouch(const char* args) {
    if (args[0] == '\0') {
        putString("\nError: touch requires a filename\n");
        return;
    }
    
    // Check if the filename has .hak extension
    const char* extension = ".hak";
    size_t argsLen = strlen(args);
    size_t extLen = strlen(extension);
    
    if (argsLen <= extLen || strcmp(args + argsLen - extLen, extension) != 0) {
        putString("\nError: Only .hak files are supported\n");
        return;
    }
    
    // Create an empty .hak file
    if (filesystem->createHakFile(args, "")) {
        putString("\nFile created: ");
        putString(args);
        putString("\n");
    } else {
        putString("\nError: Could not create file\n");
    }
}

void Terminal::cmdCat(const char* args) {
    if (args[0] == '\0') {
        putString("\nError: cat requires a filename\n");
        return;
    }
    
    // Buffer to store file content
    char buffer[FS_MAX_FILE_SIZE];
    
    // Read file content
    if (filesystem->readHakFile(args, buffer, FS_MAX_FILE_SIZE)) {
        putString("\nContent of ");
        putString(args);
        putString(":\n");
        
        // Display file content
        if (buffer[0] == '\0') {
            putString("  <empty file>\n");
        } else {
            putString(buffer);
            putString("\n");
        }
    } else {
        putString("\nError: Could not read file\n");
    }
}

void Terminal::cmdEcho(const char* args) {
    if (args[0] == '\0') {
        putString("\n");
        return;
    }
    
    // Use static buffers instead of stack arrays to avoid 64-bit stack overflow
    static char text[256];
    static char filename[256];
    
    // Clear buffers
    text[0] = '\0';
    filename[0] = '\0';
    bool redirect = false;
    
    // Find the '>' character for redirection
    int i = 0;
    int textEnd = 0;
    while (args[i] != '\0') {
        if (args[i] == '>') {
            redirect = true;
            textEnd = i;
            break;
        }
        i++;
    }
    
    if (redirect) {
        // Extract the text to echo (excluding trailing spaces)
        for (i = 0; i < textEnd; i++) {
            text[i] = args[i];
        }
        text[textEnd] = '\0';
        
        // Trim trailing spaces
        while (textEnd > 0 && text[textEnd - 1] == ' ') {
            text[--textEnd] = '\0';
        }
        
        // Extract the filename (skipping '>' and leading spaces)
        i = textEnd + 1;
        while (args[i] == ' ' || args[i] == '>') {
            i++;
        }
        
        int j = 0;
        while (args[i] != '\0') {
            filename[j++] = args[i++];
        }
        filename[j] = '\0';
        
        // Check if filename is empty
        if (filename[0] == '\0') {
            putString("\nError: No filename specified for redirection\n");
            return;
        }
        
        // Check if the filename has .hak extension
        if (!filesystem->hasExtension(filename, ".hak")) {
            putString("\nError: Only .hak files are supported\n");
            return;
        }
        
        // Write to file
        if (filesystem->createHakFile(filename, text)) {
            putString("\nContent written to ");
            putString(filename);
            putString("\n");
        } else {
            putString("\nError: Could not write to file\n");
        }
    } else {
        // Just echo the text
        putString("\n");
        putString(args);
        putString("\n");
    }
}

void Terminal::cmdNetinfo(const char* args) {
    (void)args;
    putString("\nNetwork information:\n");

    if (!network) {
        putString("  Network subsystem: unavailable\n");
        return;
    }

    const NetworkInfo& info = network->getInfo();
    char number[16];
    char hex[11];
    char mac[18];

    putString("  NIC: ");
    if (info.rtl8139Present) {
        putString(info.nicName);
    } else if (info.pciPresent) {
        putString("Unsupported Ethernet device");
    } else {
        putString("not detected");
    }
    putString("\n");

    putString("  MAC: ");
    if (info.rtl8139Present) {
        network->formatMac(mac, sizeof(mac));
        putString(mac);
    } else {
        putString("--:--:--:--:--:--");
    }
    putString("\n");

    putString("  IP: ");
    if (info.ipAddress) { network->formatIp(info.ipAddress, number, sizeof(number)); putString(number); }
    else { putString("not configured"); }
    putString("\n");

    putString("  Gateway: ");
    if (info.gatewayIp) { network->formatIp(info.gatewayIp, number, sizeof(number)); putString(number); }
    else { putString("not configured"); }
    putString("\n");

    putString("  DNS: ");
    if (info.dnsIp) { network->formatIp(info.dnsIp, number, sizeof(number)); putString(number); }
    else { putString("not configured"); }
    putString("\n");

    putString("  Netmask: ");
    if (info.netmask) { network->formatIp(info.netmask, number, sizeof(number)); putString(number); }
    else { putString("not configured"); }
    putString("\n");

    putString("  Config: ");
    putString(info.dhcpConfigured ? "DHCP" : "static/default");
    putString("\n");

    putString("  Link: ");
    putString(info.linkUp ? "up" : "unknown/down");
    putString("\n");

    putString("  Register base: ");
    u32ToHex(info.ioBase, hex, sizeof(hex));
    putString(hex);
    putString("\n");

    putString("  IRQ: ");
    u32ToDec(info.irqLine, number, sizeof(number));
    putString(number);
    putString("\n");

    putString("  RX: ");
    u32ToDec(info.rxPackets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    putString("  TX: ");
    u32ToDec(info.txPackets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    putString("  ARP: ");
    u32ToDec(info.arpPackets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    putString("  IPv4: ");
    u32ToDec(info.ipv4Packets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    putString("  ICMP: ");
    u32ToDec(info.icmpPackets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    Serial::writeString("[terminal] netinfo command displayed\n");
}

void Terminal::cmdNetpoll(const char* args) {
    (void)args;
    putString("\n");
    if (!network) {
        putString("Network subsystem unavailable\n");
        return;
    }
    network->poll();
    putString("Network RX queue polled\n");
}

void Terminal::cmdArping(const char* args) {
    putString("\n");
    if (!network) {
        putString("Network subsystem unavailable\n");
        return;
    }
    uint32_t ip;
    if (!network->parseIp(args, &ip)) {
        putString("Usage: arping 10.0.2.2\n");
        return;
    }
    putString("ARP request sent, waiting for reply...\n");
    bool ok = network->arping(ip);
    putString(ok ? "ARP reply received\n" : "ARP timeout\n");
}

void Terminal::cmdPing(const char* args) {
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }

    static char host[96];
    int i = 0; while (args[i] == ' ') i++;
    int h = 0; while (args[i] != ' ' && args[i] != '\0' && h < 95) host[h++] = args[i++]; host[h] = '\0';
    if (host[0] == '\0') { putString("Usage: ping 10.0.2.2\n"); return; }

    uint32_t ip = 0;
    if (!network->parseIp(host, &ip)) {
        putString("Resolving "); putString(host); putString("...\n");
        if (!network->resolveDnsA(host, &ip)) { putString("DNS lookup failed\n"); return; }
    }
    static char ipText[16];
    network->formatIp(ip, ipText, sizeof(ipText));
    putString("PING "); putString(host); putString(" ("); putString(ipText); putString(") 16 bytes of data.\n");

    uint32_t transmitted = 0, received = 0, minMs = 0xffffffffu, maxMs = 0, sumMs = 0;
    for (uint16_t seq = 1; seq <= 4; seq++) {
        transmitted++;
        PingResult result;
        uint32_t startMs = timerMillis();
        bool ok = network->pingOnce(ip, seq, startMs, &result);
        if (ok && result.received) {
            received++;
            uint32_t ms = result.elapsedMs;
            if (ms < minMs) minMs = ms;
            if (ms > maxMs) maxMs = ms;
            sumMs += ms;
            static char from[16]; static char num[16];
            network->formatIp(result.fromIp ? result.fromIp : ip, from, sizeof(from));
            putString("16 bytes from "); putString(from); putString(": icmp_seq=");
            u32ToDec(seq, num, sizeof(num)); putString(num);
            putString(" ttl="); u32ToDec(result.ttl, num, sizeof(num)); putString(num);
            putString(" time="); u32ToDec(ms, num, sizeof(num)); putString(num); putString(" ms\n");
        } else {
            static char num[16];
            putString("Request timeout for icmp_seq "); u32ToDec(seq, num, sizeof(num)); putString(num); putString("\n");
        }
        pitSleepMs(250);
    }

    static char num[16];
    putString("--- "); putString(host); putString(" ping statistics ---\n");
    u32ToDec(transmitted, num, sizeof(num)); putString(num); putString(" packets transmitted, ");
    u32ToDec(received, num, sizeof(num)); putString(num); putString(" received, ");
    uint32_t loss = transmitted ? ((transmitted - received) * 100u) / transmitted : 0;
    u32ToDec(loss, num, sizeof(num)); putString(num); putString("% packet loss\n");
    if (received) {
        putString("rtt min/avg/max = ");
        u32ToDec(minMs, num, sizeof(num)); putString(num); putString("/");
        u32ToDec(sumMs / received, num, sizeof(num)); putString(num); putString("/");
        u32ToDec(maxMs, num, sizeof(num)); putString(num); putString(" ms\n");
    }
}

void Terminal::cmdDhcp(const char* args) {
    (void)args;
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }
    putString("DHCP: started discover/request in the background. You can keep typing.\n");
    bool ok = network->startDhcp();
    if (!ok) { putString("DHCP could not start; NIC is unavailable or TX failed\n"); lastDhcpState = network->getDhcpState(); return; }
    lastDhcpState = network->getDhcpState();
}

void Terminal::cmdTraceroute(const char* args) {
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }
    static char host[96];
    int i = 0; while (args[i] == ' ') i++;
    int h = 0; while (args[i] != ' ' && args[i] != '\0' && h < 95) host[h++] = args[i++]; host[h] = '\0';
    if (host[0] == '\0') { putString("Usage: traceroute example.com\n"); return; }
    uint32_t ip = 0;
    if (!network->parseIp(host, &ip)) {
        putString("Resolving "); putString(host); putString("...\n");
        if (!network->resolveDnsA(host, &ip)) { putString("DNS lookup failed\n"); return; }
    }
    static char ipText[16]; static char num[16];
    network->formatIp(ip, ipText, sizeof(ipText));
    putString("traceroute to "); putString(host); putString(" ("); putString(ipText); putString("), 8 hops max\n");
    for (uint8_t ttl = 1; ttl <= 8; ttl++) {
        TraceHopResult hop;
        uint32_t startMs = timerMillis();
        bool ok = network->tracerouteHop(ip, ttl, static_cast<uint16_t>(100 + ttl), startMs, &hop);
        u32ToDec(ttl, num, sizeof(num)); putString(num); putString("  ");
        if (ok && hop.received) {
            uint32_t shownIp = hop.fromIp ? hop.fromIp : (hop.destinationReached ? ip : hop.fromIp);
            network->formatIp(shownIp, ipText, sizeof(ipText)); putString(ipText); putString("  ");
            u32ToDec(hop.elapsedMs, num, sizeof(num)); putString(num); putString(" ms");
            if (hop.destinationReached) { putString("  reached\n"); break; }
            putString("\n");
        } else {
            putString("*\n");
        }
        pitSleepMs(150);
    }
}

void Terminal::cmdUdp(const char* args) {
    putString("\n");
    if (!network) {
        putString("Network subsystem unavailable\n");
        return;
    }

    static char ipText[32];
    static char message[96];
    int i = 0;
    while (args[i] == ' ') i++;
    int p = 0;
    while (args[i] != ' ' && args[i] != '\0' && p < 31) ipText[p++] = args[i++];
    ipText[p] = '\0';
    while (args[i] == ' ') i++;
    int m = 0;
    while (args[i] != '\0' && m < 95) message[m++] = args[i++];
    message[m] = '\0';

    uint32_t ip;
    if (ipText[0] == '\0' || message[0] == '\0') {
        putString("Usage: udp 10.0.2.2 hello\n");
        return;
    }

    if (!network->parseIp(ipText, &ip)) {
        putString("Resolving ");
        putString(ipText);
        putString("...\n");
        if (!network->resolveDnsA(ipText, &ip)) {
            putString("DNS lookup failed\n");
            return;
        }
        char resolved[16];
        network->formatIp(ip, resolved, sizeof(resolved));
        putString("Resolved to ");
        putString(resolved);
        putString("\n");
    }

    putString("Sending UDP text to ");
    putString(ipText);
    putString(":9001...\n");
    bool ok = network->sendUdpText(ip, 9001, message);
    putString(ok ? "UDP packet sent\n" : "UDP send failed\n");
}

void Terminal::cmdDns(const char* args) {
    putString("\n");
    if (!network) {
        putString("Network subsystem unavailable\n");
        return;
    }

    static char hostname[128];
    int i = 0;
    while (args[i] == ' ') i++;
    int h = 0;
    while (args[i] != ' ' && args[i] != '\0' && h < 127) hostname[h++] = args[i++];
    hostname[h] = '\0';
    if (hostname[0] == '\0') {
        putString("Usage: dns example.com\n");
        return;
    }

    char dnsText[16];
    network->formatIp(network->getInfo().dnsIp, dnsText, sizeof(dnsText));
    putString("Resolving ");
    putString(hostname);
    putString(" using DNS ");
    putString(dnsText);
    putString("...\n");
    uint32_t ip = 0;
    bool ok = network->resolveDnsA(hostname, &ip);
    if (ok) {
        char ipText[16];
        network->formatIp(ip, ipText, sizeof(ipText));
        putString("A ");
        putString(hostname);
        putString(" = ");
        putString(ipText);
        putString("\n");
    } else {
        putString("DNS lookup failed or timed out\n");
    }
}

void Terminal::cmdTcp(const char* args) {
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }

    static char hostText[64]; static char portText[8]; static char message[128];
    int i = 0; while (args[i] == ' ') i++;
    int h = 0; while (args[i] != ' ' && args[i] != '\0' && h < 63) hostText[h++] = args[i++]; hostText[h] = '\0';
    while (args[i] == ' ') i++;
    int p = 0; while (args[i] != ' ' && args[i] != '\0' && p < 7) portText[p++] = args[i++]; portText[p] = '\0';
    while (args[i] == ' ') i++;
    int m = 0; while (args[i] != '\0' && m < 127) message[m++] = args[i++]; message[m] = '\0';
    if (hostText[0] == '\0' || portText[0] == '\0' || message[0] == '\0') { putString("Usage: tcp 1.1.1.1 80 hello\n"); return; }

    uint32_t ip = 0;
    if (!network->parseIp(hostText, &ip)) {
        putString("Resolving "); putString(hostText); putString("...\n");
        if (!network->resolveDnsA(hostText, &ip)) { putString("DNS lookup failed\n"); return; }
    }
    uint32_t port = 0;
    for (int j = 0; portText[j]; j++) { if (portText[j] < '0' || portText[j] > '9') { putString("TCP port must be numeric\n"); return; } port = port * 10 + static_cast<uint32_t>(portText[j] - '0'); }
    if (port == 0 || port > 65535) { putString("TCP port must be 1..65535\n"); return; }

    putString("TCP connect/send to "); putString(hostText); putString(":"); putString(portText); putString("...\n");
    bool ok = network->sendTcpText(ip, static_cast<uint16_t>(port), message);
    putString(ok ? "TCP text sent\n" : "TCP connect/send failed\n");
}

void Terminal::cmdHttp(const char* args) {
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }
    static char host[64]; static char req[180]; static char resp[512];
    int i = 0; while (args[i] == ' ') i++;
    int h = 0; while (args[i] != ' ' && args[i] != '\0' && h < 63) host[h++] = args[i++]; host[h] = '\0';
    if (host[0] == '\0') { putString("Usage: http example.com\n"); return; }
    uint32_t ip = 0;
    if (!network->parseIp(host, &ip)) {
        putString("Resolving "); putString(host); putString("...\n");
        if (!network->resolveDnsA(host, &ip)) { putString("DNS lookup failed\n"); return; }
    }
    int r = 0; const char* a = "GET / HTTP/1.0\r\nHost: ";
    for (int j = 0; a[j] && r < 179; j++) req[r++] = a[j];
    for (int j = 0; host[j] && r < 179; j++) req[r++] = host[j];
    const char* b = "\r\nConnection: close\r\n\r\n";
    for (int j = 0; b[j] && r < 179; j++) req[r++] = b[j];
    req[r] = 0;
    putString("HTTP GET http://"); putString(host); putString("/ ...\n");
    bool ok = network->tcpRequestText(ip, 80, req, resp, sizeof(resp));
    if (!ok) { putString("HTTP TCP request failed\n"); return; }
    if (resp[0]) { putString(resp); putString("\n"); }
    else putString("Connected, but no HTTP data received\n");
}


static bool curlIsMethodChar(char c) {
    return (c >= 'A' && c <= 'Z') || c == '-';
}

static bool curlMethodAllowed(const char* m) {
    return strcmp(m, "GET") == 0 || strcmp(m, "POST") == 0 || strcmp(m, "PUT") == 0 ||
           strcmp(m, "PATCH") == 0 || strcmp(m, "DELETE") == 0 || strcmp(m, "HEAD") == 0 ||
           strcmp(m, "OPTIONS") == 0 || strcmp(m, "TRACE") == 0 || strcmp(m, "CONNECT") == 0;
}

static void appendStr(char* out, int* pos, int cap, const char* text) {
    if (!out || !pos || cap <= 0 || !text) return;
    while (*text && *pos < cap - 1) out[(*pos)++] = *text++;
    out[*pos] = '\0';
}

static void appendDec(char* out, int* pos, int cap, uint32_t value) {
    char tmp[16];
    u32ToDec(value, tmp, sizeof(tmp));
    appendStr(out, pos, cap, tmp);
}

void Terminal::cmdCurl(const char* args) {
    putString("\nMrHakOS curl HTTP client\n");
    putString("  Transport: direct HTTP over TCP only; HTTPS/TLS URL fetch is not implemented yet\n");
    if (!network) { putString("  Network subsystem unavailable\n"); return; }
    const NetworkInfo& info = network->getInfo();
    if (!info.rtl8139Present || !info.rxEnabled) { putString("  Network: blocked; Ethernet is not online\n"); return; }
    if (info.ipAddress == 0 || info.gatewayIp == 0) { putString("  Network: blocked; run dhcp first\n"); return; }

    static char method[12];
    static char url[128];
    static char body[256];
    static char host[64];
    static char path[96];
    static char req[1024];
    static char resp[2048];
    method[0] = 'G'; method[1] = 'E'; method[2] = 'T'; method[3] = '\0';
    url[0] = '\0'; body[0] = '\0'; host[0] = '\0'; path[0] = '/'; path[1] = '\0';

    int i = 0;
    while (args[i] == ' ') i++;
    if (args[i] == '\0') {
        putString("  Usage:\n");
        putString("    curl http://host[:port]/path\n");
        putString("    curl -X POST http://host/path body\n");
        putString("    curl POST http://host/path body\n");
        putString("  Methods: GET POST PUT PATCH DELETE HEAD OPTIONS TRACE CONNECT\n");
        return;
    }

    if (args[i] == '-' && args[i+1] == 'X') {
        i += 2;
        while (args[i] == ' ') i++;
        int m = 0;
        while (curlIsMethodChar(args[i]) && m < 11) method[m++] = args[i++];
        method[m] = '\0';
        while (args[i] == ' ') i++;
    } else {
        int save = i;
        static char first[12];
        int f = 0;
        while (curlIsMethodChar(args[i]) && f < 11) first[f++] = args[i++];
        first[f] = '\0';
        if (curlMethodAllowed(first) && args[i] == ' ') {
            int m = 0; while (first[m]) { method[m] = first[m]; m++; } method[m] = '\0';
            while (args[i] == ' ') i++;
        } else {
            i = save;
        }
    }
    if (!curlMethodAllowed(method)) { putString("  Unsupported method. Supported: GET POST PUT PATCH DELETE HEAD OPTIONS TRACE CONNECT\n"); return; }

    int u = 0;
    while (args[i] != ' ' && args[i] != '\0' && u < 127) url[u++] = args[i++];
    url[u] = '\0';
    while (args[i] == ' ') i++;
    int b = 0;
    while (args[i] != '\0' && b < 255) body[b++] = args[i++];
    body[b] = '\0';
    if (url[0] == '\0') { putString("  URL missing\n"); return; }

    const char* purl = url;
    if (lineStartsWith(purl, "http://")) purl += 7;
    else if (lineStartsWith(purl, "https://")) { putString("  HTTPS URL blocked: kernel TLS fetch is not implemented yet\n"); return; }

    int h = 0;
    uint32_t port = 80;
    while (*purl && *purl != '/' && *purl != ':' && h < 63) host[h++] = *purl++;
    host[h] = '\0';
    if (*purl == ':') {
        purl++;
        port = 0;
        while (*purl >= '0' && *purl <= '9') { port = port * 10u + static_cast<uint32_t>(*purl - '0'); purl++; }
        if (port == 0 || port > 65535) { putString("  Invalid port\n"); return; }
    }
    if (*purl == '/') {
        int pp = 0;
        while (*purl && pp < 95) path[pp++] = *purl++;
        path[pp] = '\0';
    }
    if (host[0] == '\0') { putString("  Host missing\n"); return; }

    uint32_t ip = 0;
    if (!network->parseIp(host, &ip)) {
        putString("  Resolving "); putString(host); putString("...\n");
        if (!network->resolveDnsA(host, &ip)) { putString("  DNS lookup failed\n"); return; }
    }

    int r = 0;
    appendStr(req, &r, sizeof(req), method);
    appendStr(req, &r, sizeof(req), " ");
    if (strcmp(method, "CONNECT") == 0) {
        appendStr(req, &r, sizeof(req), host);
        appendStr(req, &r, sizeof(req), ":");
        appendDec(req, &r, sizeof(req), port);
    } else {
        appendStr(req, &r, sizeof(req), path);
    }
    appendStr(req, &r, sizeof(req), " HTTP/1.0\r\nHost: ");
    appendStr(req, &r, sizeof(req), host);
    appendStr(req, &r, sizeof(req), "\r\nUser-Agent: MrHakOS-curl/0.1\r\nConnection: close\r\n");
    if (body[0] && (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0 || strcmp(method, "DELETE") == 0)) {
        appendStr(req, &r, sizeof(req), "Content-Type: text/plain\r\nContent-Length: ");
        appendDec(req, &r, sizeof(req), static_cast<uint32_t>(b));
        appendStr(req, &r, sizeof(req), "\r\n");
    }
    appendStr(req, &r, sizeof(req), "\r\n");
    if (body[0] && (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0 || strcmp(method, "DELETE") == 0)) {
        appendStr(req, &r, sizeof(req), body);
    }

    static char num[16];
    putString("  HTTP "); putString(method); putString(" http://"); putString(host); putString(path); putString(" ...\n");
    bool ok = network->tcpRequestText(ip, static_cast<uint16_t>(port), req, resp, sizeof(resp));
    if (!ok) { putString("  curl: TCP request failed\n"); return; }
    u32ToDec(static_cast<uint32_t>(r), num, sizeof(num)); putString("  Request bytes: "); putString(num); putString("\n");
    if (resp[0]) { putString(resp); putString("\n"); }
    else putString("  Connected, but no HTTP data received\n");
}

static bool parsePort16(const char* text, uint16_t* out) {
    if (!text || text[0] == '\0' || !out) return false;
    uint32_t port = 0;
    for (int j = 0; text[j]; j++) {
        if (text[j] < '0' || text[j] > '9') return false;
        port = port * 10u + static_cast<uint32_t>(text[j] - '0');
        if (port > 65535u) return false;
    }
    if (port == 0) return false;
    *out = static_cast<uint16_t>(port);
    return true;
}

static const char* socks5ReplyName(uint8_t rep) {
    switch (rep) {
        case 0x00: return "succeeded";
        case 0x01: return "general SOCKS server failure";
        case 0x02: return "connection not allowed by ruleset";
        case 0x03: return "network unreachable";
        case 0x04: return "host unreachable";
        case 0x05: return "connection refused";
        case 0x06: return "TTL expired";
        case 0x07: return "command not supported";
        case 0x08: return "address type not supported";
        default:   return "unknown reply code";
    }
}

static const char* socks5PhaseName(uint8_t phase) {
    switch (phase) {
        case 0: return "complete";
        case 1: return "TCP connect to proxy";
        case 2: return "greeting / method selection";
        case 3: return "no acceptable auth method offered by proxy";
        case 4: return "proxy required username/password but none was given";
        case 5: return "username/password exchange";
        case 6: return "username/password rejected";
        case 7: return "CONNECT request send";
        case 8: return "CONNECT reply parse";
        case 9: return "CONNECT rejected by proxy";
        default: return "unknown";
    }
}

void Terminal::cmdSocks5(const char* args) {
    putString("\nSOCKS5 proxy tunnel\n");
    putString("  Transport: TCP CONNECT through an external SOCKS5 proxy (RFC 1928 / 1929)\n");
    putString("  Link: runs over the DHCP-configured Ethernet; domain targets resolve at the proxy\n");

    if (!network) { putString("  Network subsystem unavailable\n"); return; }
    const NetworkInfo& info = network->getInfo();
    if (!info.rtl8139Present || !info.rxEnabled) { putString("  Network: blocked; Ethernet is not online\n"); return; }
    if (info.ipAddress == 0 || info.gatewayIp == 0) { putString("  Network: blocked; run dhcp first\n"); return; }

    static char username[64];
    static char password[64];
    static char proxyText[64];
    static char proxyPortText[8];
    static char destText[128];
    static char destPortText[8];
    static char payload[256];
    username[0] = '\0'; password[0] = '\0';
    proxyText[0] = '\0'; proxyPortText[0] = '\0';
    destText[0] = '\0'; destPortText[0] = '\0'; payload[0] = '\0';

    int i = 0;
    while (args[i] == ' ') i++;
    if (args[i] == '\0') {
        putString("  Usage:\n");
        putString("    socks5 [-u user:pass] <proxyIp> <proxyPort> <destHost|destIp> <destPort> [http|text...]\n");
        putString("  Examples:\n");
        putString("    socks5 10.0.2.2 9050 example.com 80 http\n");
        putString("    socks5 -u alice:secret 10.0.2.2 1080 1.1.1.1 80 http\n");
        return;
    }

    // Optional -u user:pass (the proxy is trusted/local, so credentials stay local).
    if (args[i] == '-' && args[i + 1] == 'u' && (args[i + 2] == ' ' || args[i + 2] == '\0')) {
        i += 2;
        while (args[i] == ' ') i++;
        int u = 0;
        while (args[i] != ':' && args[i] != ' ' && args[i] != '\0' && u < 63) username[u++] = args[i++];
        username[u] = '\0';
        if (args[i] == ':') {
            i++;
            int pw = 0;
            while (args[i] != ' ' && args[i] != '\0' && pw < 63) password[pw++] = args[i++];
            password[pw] = '\0';
        }
        while (args[i] == ' ') i++;
    }

    int p = 0; while (args[i] != ' ' && args[i] != '\0' && p < 63) proxyText[p++] = args[i++]; proxyText[p] = '\0';
    while (args[i] == ' ') i++;
    int pp = 0; while (args[i] != ' ' && args[i] != '\0' && pp < 7) proxyPortText[pp++] = args[i++]; proxyPortText[pp] = '\0';
    while (args[i] == ' ') i++;
    int d = 0; while (args[i] != ' ' && args[i] != '\0' && d < 127) destText[d++] = args[i++]; destText[d] = '\0';
    while (args[i] == ' ') i++;
    int dp = 0; while (args[i] != ' ' && args[i] != '\0' && dp < 7) destPortText[dp++] = args[i++]; destPortText[dp] = '\0';
    while (args[i] == ' ') i++;
    int pl = 0; while (args[i] != '\0' && pl < 255) payload[pl++] = args[i++]; payload[pl] = '\0';

    if (proxyText[0] == '\0' || proxyPortText[0] == '\0' || destText[0] == '\0' || destPortText[0] == '\0') {
        putString("  Usage: socks5 [-u user:pass] <proxyIp> <proxyPort> <destHost|destIp> <destPort> [http|text...]\n");
        return;
    }

    uint32_t proxyIp = 0;
    if (!network->parseIp(proxyText, &proxyIp)) {
        putString("  Resolving proxy "); putString(proxyText); putString("...\n");
        if (!network->resolveDnsA(proxyText, &proxyIp)) { putString("  Proxy DNS lookup failed\n"); return; }
    }
    uint16_t proxyPort = 0;
    if (!parsePort16(proxyPortText, &proxyPort)) { putString("  Proxy port must be 1..65535\n"); return; }
    uint16_t destPort = 0;
    if (!parsePort16(destPortText, &destPort)) { putString("  Destination port must be 1..65535\n"); return; }

    // A literal IPv4 destination uses ATYP 0x01; anything else is a domain (ATYP 0x03)
    // that the proxy resolves, which is what keeps the lookup off the local link.
    uint32_t destIp = 0;
    bool destIsIp = network->parseIp(destText, &destIp);

    // Build the optional application payload sent once the tunnel is open.
    static uint8_t appBuf[512];
    uint16_t appLen = 0;
    bool wantHttp = (strcmp(payload, "http") == 0 || strcmp(payload, "HTTP") == 0);
    if (wantHttp) {
        int r = 0;
        const char* a1 = "GET / HTTP/1.0\r\nHost: ";
        for (int j = 0; a1[j] && r < 500; j++) appBuf[r++] = static_cast<uint8_t>(a1[j]);
        for (int j = 0; destText[j] && r < 500; j++) appBuf[r++] = static_cast<uint8_t>(destText[j]);
        const char* a2 = "\r\nUser-Agent: MrHakOS-socks5/0.1\r\nConnection: close\r\n\r\n";
        for (int j = 0; a2[j] && r < 500; j++) appBuf[r++] = static_cast<uint8_t>(a2[j]);
        appLen = static_cast<uint16_t>(r);
    } else if (payload[0] != '\0') {
        int r = 0;
        for (int j = 0; payload[j] && r < 500; j++) appBuf[r++] = static_cast<uint8_t>(payload[j]);
        appLen = static_cast<uint16_t>(r);
    }

    static char num[16];
    putString("  Proxy: "); putString(proxyText); putString(":"); putString(proxyPortText);
    putString("   Auth: "); putString(username[0] ? "username/password\n" : "none\n");
    putString("  Target: "); putString(destText); putString(":"); putString(destPortText);
    putString(destIsIp ? "  (ATYP ipv4)\n" : "  (ATYP domain; resolved at proxy)\n");
    putString("  Connecting through proxy...\n");

    static uint8_t resp[2048];
    uint16_t respLen = 0;
    Socks5Result res;
    network->socks5Connect(proxyIp, proxyPort,
                           destIsIp ? static_cast<const char*>(0) : destText,
                           destIsIp ? destIp : 0u,
                           destPort,
                           username[0] ? username : static_cast<const char*>(0),
                           password[0] ? password : static_cast<const char*>(0),
                           appLen ? appBuf : static_cast<const uint8_t*>(0), appLen,
                           resp, static_cast<uint16_t>(sizeof(resp) - 1), &respLen, &res);

    putString("  Proxy TCP: "); putString(res.tcpConnected ? "connected\n" : "failed\n");
    if (res.methodSelected) {
        putString("  Method: ");
        if (res.method == 0x00) putString("no-auth\n");
        else if (res.method == 0x02) putString("username/password\n");
        else if (res.method == 0xFF) putString("none acceptable\n");
        else { u32ToHex(res.method, num, sizeof(num)); putString(num); putString("\n"); }
    }
    if (res.authPerformed) { putString("  Auth: "); putString(res.authOk ? "accepted\n" : "rejected\n"); }
    if (res.tcpConnected) {
        putString("  CONNECT: ");
        if (res.connectOk) { putString("succeeded\n"); }
        else { putString(socks5ReplyName(res.replyCode)); putString("\n"); }
    }
    if (res.connectOk && res.boundAtyp == 0x01 && res.boundIp != 0) {
        char bound[16];
        network->formatIp(res.boundIp, bound, sizeof(bound));
        putString("  Bound: "); putString(bound); putString(":");
        u32ToDec(res.boundPort, num, sizeof(num)); putString(num); putString("\n");
    }

    if (!res.connectOk) {
        putString("  Stopped at: "); putString(socks5PhaseName(res.failPhase)); putString("\n");
        putString("  Result: tunnel not established (fail-closed; no direct fallback attempted)\n");
        return;
    }

    putString("  Tunnel: established via SOCKS5 CONNECT\n");
    if (appLen) {
        u32ToDec(res.appResponseLen, num, sizeof(num));
        putString("  Response ("); putString(num); putString(" bytes):\n");
        if (res.appResponseLen) {
            uint16_t n = res.appResponseLen;
            if (n > sizeof(resp) - 1) n = static_cast<uint16_t>(sizeof(resp) - 1);
            resp[n] = 0;
            putString(reinterpret_cast<const char*>(resp));
            putString("\n");
        } else {
            putString("  Tunnel open, but no application data was received\n");
        }
    } else {
        putString("  No application payload sent; add 'http' or raw text to fetch through the tunnel\n");
    }
}

void Terminal::cmdTor(const char* args) {
    putString("\nNative Tor client\n");
    putString("  Policy: native Tor only for default path; no SOCKS/proxy fallback\n");
    putString("  State: bootstrap stages are enforced fail-closed until circuits/streams exist\n");

    const char* earlyArgs = args;
    while (*earlyArgs == ' ') earlyArgs++;
    if (strcmp(earlyArgs, "crypto") == 0) {
        putString("  Native Tor crypto foundation\n");
        bool ok = crypto_self_test();
        putString("  SHA256: "); putString(ok ? "self-test PASS\n" : "self-test FAIL\n");
        putString("  HMAC-SHA256: "); putString(ok ? "self-test PASS\n" : "self-test FAIL\n");
        putString("  TLS/Tor status: primitives are compiled in, but not wired into TLS records yet\n");
        putString("  Remaining crypto: secure RNG, x25519, AES-GCM/CBC, TLS PRF/Finished, Tor relay ciphers\n");
        return;
    }

    if (!network) {
        putString("  Network: unavailable\n");
        return;
    }

    const NetworkInfo& info = network->getInfo();
    if (!info.rtl8139Present || !info.rxEnabled) {
        putString("  Network: blocked; Ethernet is not online\n");
        return;
    }
    if (info.ipAddress == 0 || info.gatewayIp == 0) {
        putString("  Network: blocked; run dhcp first\n");
        return;
    }

    static char firstArg[64];
    static char secondArg[16];
    static char thirdArg[16];
    firstArg[0] = '\0';
    secondArg[0] = '\0';
    thirdArg[0] = '\0';
    int ai = 0;
    while (args[ai] == ' ') ai++;
    int fa = 0;
    while (args[ai] != ' ' && args[ai] != '\0' && fa < 63) firstArg[fa++] = args[ai++];
    firstArg[fa] = '\0';
    while (args[ai] == ' ') ai++;
    int sa = 0;
    while (args[ai] != ' ' && args[ai] != '\0' && sa < 15) secondArg[sa++] = args[ai++];
    secondArg[sa] = '\0';
    while (args[ai] == ' ') ai++;
    int ta = 0;
    while (args[ai] != ' ' && args[ai] != '\0' && ta < 15) thirdArg[ta++] = args[ai++];
    thirdArg[ta] = '\0';

    bool proxyMode = (strcmp(firstArg, "proxy") == 0 || strcmp(firstArg, "socks") == 0 || strcmp(firstArg, "socks5") == 0);

    if (proxyMode) {
        uint32_t proxyIp = 0;
        uint16_t proxyPort = 9050;
        const char* proxyText = secondArg[0] ? secondArg : "10.0.2.2";
        const char* portText = thirdArg[0] ? thirdArg : "9050";
        if (!network->parseIp(proxyText, &proxyIp)) {
            putString("  Proxy IP invalid. Usage: tor proxy [proxyIp proxyPort]\n");
            putString("  Example in QEMU after starting host Tor: tor proxy 10.0.2.2 9050\n");
            return;
        }
        if (!parsePort16(portText, &proxyPort)) {
            putString("  Proxy port must be 1..65535\n");
            return;
        }

        static uint8_t request[256];
        static uint8_t response[2048];
        int r = 0;
        appendStr(reinterpret_cast<char*>(request), &r, sizeof(request), "GET /?format=text HTTP/1.0\r\n");
        appendStr(reinterpret_cast<char*>(request), &r, sizeof(request), "Host: api.ipify.org\r\n");
        appendStr(reinterpret_cast<char*>(request), &r, sizeof(request), "User-Agent: MrHakOS-Tor/0.1\r\n");
        appendStr(reinterpret_cast<char*>(request), &r, sizeof(request), "Connection: close\r\n\r\n");

        Socks5Result res;
        uint16_t respLen = 0;
        putString("  Connecting to Tor SOCKS5 proxy "); putString(proxyText); putString(":"); putString(portText); putString("...\n");
        bool ok = network->socks5Connect(proxyIp, proxyPort,
                                         "api.ipify.org", 0u, 80,
                                         static_cast<const char*>(0), static_cast<const char*>(0),
                                         request, static_cast<uint16_t>(r),
                                         response, static_cast<uint16_t>(sizeof(response) - 1), &respLen, &res);
        putString("  Proxy TCP: "); putString(res.tcpConnected ? "connected\n" : "failed\n");
        if (res.methodSelected) {
            putString("  SOCKS method: ");
            if (res.method == 0x00) putString("no-auth\n");
            else if (res.method == 0x02) putString("username/password\n");
            else if (res.method == 0xFF) putString("none acceptable\n");
            else putString("unknown\n");
        }
        putString("  CONNECT api.ipify.org:80: ");
        putString(res.connectOk ? "succeeded\n" : "failed\n");
        if (!ok || !res.connectOk) {
            putString("  Stopped at: "); putString(socks5PhaseName(res.failPhase)); putString("\n");
            putString("  Result: Tor not connected; fail-closed, no direct HTTP/DNS fallback attempted\n");
            return;
        }

        response[respLen < sizeof(response) ? respLen : sizeof(response) - 1] = 0;
        const char* body = reinterpret_cast<const char*>(response);
        for (uint16_t j = 0; j + 3 < respLen; j++) {
            if (response[j] == '\r' && response[j + 1] == '\n' && response[j + 2] == '\r' && response[j + 3] == '\n') {
                body = reinterpret_cast<const char*>(response + j + 4);
                break;
            }
        }
        static char ipText[40];
        int ipn = 0;
        for (int j = 0; body[j] && ipn < 39; j++) {
            char c = body[j];
            if ((c >= '0' && c <= '9') || c == '.') ipText[ipn++] = c;
            else if (ipn > 0) break;
        }
        ipText[ipn] = '\0';
        static char num[16];
        u32ToDec(res.appResponseLen, num, sizeof(num)); putString("  Response bytes through Tor: "); putString(num); putString("\n");
        if (ipText[0]) {
            putString("  Your Tor exit/public IP: "); putString(ipText); putString("\n");
            putString("  Secure mode: ON (SOCKS5 tunnel established; remote DNS via proxy; no clearnet fallback)\n");
        } else {
            putString("  Tunnel worked, but IP body was not parsed. Raw response follows:\n");
            putString(reinterpret_cast<const char*>(response)); putString("\n");
        }
        return;
    }

    if (firstArg[0] == '\0' || strcmp(firstArg, "native") == 0 || strcmp(firstArg, "start") == 0 || strcmp(firstArg, "auto") == 0) {
        putString("  Native bootstrap sequence starting (no external Tor/SOCKS proxy)\n");
        putString("  Step 1/3: directory consensus\n");
        cmdTor("consensus");
        if (!torDirectoryReachable || !torSelectedNickname[0]) {
            putString("  Native Tor: stopped fail-closed at consensus/guard selection\n");
            return;
        }
        putString("  Step 2/3: ORPort TLS transport probe\n");
        cmdTor("tls");
        if (!torTlsReady) {
            putString("  Native Tor: stopped fail-closed at TLS transport\n");
            return;
        }
        putString("  Step 3/3: circuit/stream gate\n");
        cmdTor("circuit");
        if (!torCircuitsReady) {
            putString("  Native Tor: not a complete client yet; circuits/streams are still blocked\n");
            putString("  Required next implementation: TLS key schedule, encrypted Tor cells, ntor, RELAY streams\n");
            return;
        }
        putString("  Native Tor: circuits ready\n");
        return;
    }

    if (strcmp(firstArg, "status") == 0) {
        static char num[16];
        putString("  Directory: ");
        putString(torDirectoryReachable ? "reachable\n" : "not checked/reachable yet\n");
        if (torDirectoryReachable) {
            u32ToDec(torRelayCount, num, sizeof(num)); putString("  Relays parsed in sample: "); putString(num); putString("\n");
            u32ToDec(torGuardCount, num, sizeof(num)); putString("  Guards: "); putString(num); putString("\n");
            u32ToDec(torExitCount, num, sizeof(num)); putString("  Exits: "); putString(num); putString("\n");
            u32ToDec(torFastCount, num, sizeof(num)); putString("  Fast: "); putString(num); putString("\n");
            u32ToDec(torStableCount, num, sizeof(num)); putString("  Stable: "); putString(num); putString("\n");
            u32ToDec(torRunningCount, num, sizeof(num)); putString("  Running: "); putString(num); putString("\n");
            u32ToDec(torValidCount, num, sizeof(num)); putString("  Valid: "); putString(num); putString("\n");
            u32ToDec(torUsableGuardCount, num, sizeof(num)); putString("  Usable guards: "); putString(num); putString("\n");
            if (torSelectedNickname[0]) {
                putString("  Selected guard candidate:\n");
                putString("    nickname: "); putString(torSelectedNickname); putString("\n");
                putString("    ip: "); putString(torSelectedIp); putString("\n");
                u32ToDec(torSelectedOrPort, num, sizeof(num)); putString("    orport: "); putString(num); putString("\n");
                putString("    flags: Guard");
                if (torSelectedHasFast) putString(" Fast");
                if (torSelectedHasStable) putString(" Stable");
                if (torSelectedHasRunning) putString(" Running");
                if (torSelectedHasValid) putString(" Valid");
                if (torSelectedHasExit) putString(" Exit");
                putString("\n");
            }
        }
        putString("  TLS to selected ORPort: ");
        putString(torTlsReady ? "server handshake record seen\n" : "not checked/ready yet\n");
        if (torTlsRxLen) {
            u32ToDec(torTlsRxLen, num, sizeof(num)); putString("  TLS bytes received: "); putString(num); putString("\n");
            if (torTlsRecordType) {
                putString("  TLS last record: "); putString(tlsRecordTypeName(torTlsRecordType));
                putString(" len="); u32ToDec(torTlsRecordLen, num, sizeof(num)); putString(num); putString("\n");
                if (torTlsHandshakeType) { putString("  TLS last handshake: "); putString(tlsHandshakeTypeName(torTlsHandshakeType)); putString("\n"); }
                if (torTlsAlertDescription) { putString("  TLS last alert: "); putString(tlsAlertDescriptionName(torTlsAlertDescription)); putString("\n"); }
            }
        }
        putString("  Circuits: ");
        putString(torCircuitsReady ? "ready\n" : "not implemented yet\n");
        putString("  Next command: ");
        if (!torDirectoryReachable) putString("tor consensus\n");
        else if (!torTlsReady) putString("tor tls\n");
        else putString("tor circuit\n");
        return;
    }

    if (strcmp(firstArg, "crypto") == 0) {
        putString("  Native Tor crypto foundation\n");
        bool ok = crypto_self_test();
        putString("  SHA256: "); putString(ok ? "self-test PASS\n" : "self-test FAIL\n");
        putString("  HMAC-SHA256: "); putString(ok ? "self-test PASS\n" : "self-test FAIL\n");
        putString("  TLS/Tor status: primitives are compiled in, but not wired into TLS records yet\n");
        putString("  Remaining crypto: secure RNG, x25519, AES-GCM/CBC, TLS PRF/Finished, Tor relay ciphers\n");
        return;
    }

    if (strcmp(firstArg, "tls") == 0) {
        if (!torSelectedNickname[0] || !torSelectedIp[0] || torSelectedOrPort == 0) {
            putString("  No selected guard yet; running tor consensus first...\n");
            cmdTor("consensus");
            if (!torSelectedNickname[0] || !torSelectedIp[0] || torSelectedOrPort == 0) {
                putString("  TLS: blocked; no usable Guard candidate in consensus sample\n");
                return;
            }
        }

        uint32_t guardIp = 0;
        if (!network->parseIp(torSelectedIp, &guardIp)) {
            putString("  TLS: blocked; selected Guard IP is invalid\n");
            return;
        }

        static uint8_t tlsResponse[4096];
        uint16_t tlsLen = 0;
        static char num[16];
        putString("  TLS stream to selected Guard ORPort\n");
        putString("    nickname: "); putString(torSelectedNickname); putString("\n");
        putString("    ip: "); putString(torSelectedIp); putString("\n");
        u32ToDec(torSelectedOrPort, num, sizeof(num)); putString("    orport: "); putString(num); putString("\n");
        putString("  Opening persistent TCP stream...\n");
        bool ok = network->tcpStreamOpen(guardIp, static_cast<uint16_t>(torSelectedOrPort));
        if (!ok) {
            torTlsReady = false;
            putString("  TLS: TCP stream open failed\n");
            putString("  Circuits: blocked; no TLS transport\n");
            return;
        }
        putString("  Sending TLS 1.2 ClientHello on kept-open stream...\n");
        ok = network->tcpStreamSend(TOR_TLS_CLIENT_HELLO, sizeof(TOR_TLS_CLIENT_HELLO));
        if (ok) {
            tlsLen = network->tcpStreamDrain(tlsResponse, sizeof(tlsResponse), 900, 9000);
        }
        network->tcpStreamClose();
        torTlsRxLen = tlsLen;
        TlsParsedRecord rec;
        bool parsed = parseTlsRecord(tlsResponse, tlsLen, &rec);
        TlsHandshakeScan scan;
        scanTlsHandshakeRecords(tlsResponse, tlsLen, &scan);
        torTlsRecordType = rec.recordType;
        torTlsMajor = rec.major;
        torTlsMinor = rec.minor;
        torTlsRecordLen = rec.recordLen;
        torTlsHandshakeType = rec.handshakeType;
        torTlsHandshakeLen = rec.handshakeLen;
        torTlsAlertLevel = rec.alertLevel;
        torTlsAlertDescription = rec.alertDescription;
        torTlsHandshakeSeen = scan.serverHello;
        torTlsReady = ok && torTlsHandshakeSeen;
        if (!ok) {
            putString("  TLS: ClientHello send failed\n");
            putString("  Circuits: blocked; no TLS transport\n");
            return;
        }
        u32ToDec(tlsLen, num, sizeof(num)); putString("  TLS bytes received before close: "); putString(num); putString("\n");
        if (tlsLen >= 5) {
            char hx[3];
            putString("  First TLS record header: ");
            for (int b = 0; b < 5; b++) { byteToHex(tlsResponse[b], hx); putString(hx); if (b != 4) putString(" "); }
            putString("\n");
        }
        if (parsed) {
            putString("  First TLS record: "); putString(tlsRecordTypeName(rec.recordType));
            putString(" v"); u32ToDec(rec.major, num, sizeof(num)); putString(num); putString(".");
            u32ToDec(rec.minor, num, sizeof(num)); putString(num);
            putString(" len="); u32ToDec(rec.recordLen, num, sizeof(num)); putString(num); putString("\n");
            if (rec.isHandshake) {
                putString("  First TLS handshake: "); putString(tlsHandshakeTypeName(rec.handshakeType));
                putString(" len="); u32ToDec(rec.handshakeLen, num, sizeof(num)); putString(num); putString("\n");
            }
            if (rec.isAlert) {
                putString("  TLS alert: level="); u32ToDec(rec.alertLevel, num, sizeof(num)); putString(num);
                putString(" desc="); u32ToDec(rec.alertDescription, num, sizeof(num)); putString(num);
                putString(" ("); putString(tlsAlertDescriptionName(rec.alertDescription)); putString(")\n");
            }
        }
        u32ToDec(scan.records, num, sizeof(num)); putString("  TLS records scanned: "); putString(num); putString("\n");
        u32ToDec(scan.handshakes, num, sizeof(num)); putString("  TLS handshakes scanned: "); putString(num); putString("\n");
        putString("  ServerHello: "); putString(scan.serverHello ? "yes\n" : "no\n");
        putString("  Certificate: "); putString(scan.certificate ? "yes\n" : "no\n");
        putString("  ServerKeyExchange: "); putString(scan.serverKeyExchange ? "yes\n" : "no\n");
        putString("  ServerHelloDone: "); putString(scan.serverHelloDone ? "yes\n" : "no\n");
        if (scan.firstCipherSuite) {
            char hx16[7]; printHex16ToBuf(scan.firstCipherSuite, hx16);
            putString("  Selected TLS cipher: "); putString(hx16); putString(" "); putString(tlsCipherSuiteName(scan.firstCipherSuite)); putString("\n");
        }
        if (scan.selectedCurve) {
            char hx16[7]; printHex16ToBuf(scan.selectedCurve, hx16);
            putString("  Selected ECDHE group: "); putString(hx16); putString(" "); putString(tlsCurveName(scan.selectedCurve)); putString("\n");
        }
        if (torTlsHandshakeSeen) {
            putString("  TLS: live ORPort stream stays open long enough to collect server handshake records\n");
            putString("  Next code step: add freestanding SHA256/HMAC, x25519, AES, TLS Finished, then encrypted Tor cells\n");
        } else if (parsed) {
            putString("  TLS: record parsed, but no ServerHello was found in captured records\n");
            putString("  Next: tune ClientHello/cipher list or parse more stream data\n");
        } else {
            putString("  TLS: response was not a TLS record\n");
        }
        torCircuitsReady = false;
        putString("  Circuits: not ready yet\n");
        putString("  Safe result: onion apps still blocked until full TLS crypto, Tor cells, ntor, circuits, and streams exist\n");
        return;
    }

    if (strcmp(firstArg, "circuit") == 0 || strcmp(firstArg, "circuits") == 0) {
        putString("  Circuit bootstrap control\n");
        if (!torTlsReady) {
            putString("  TLS: not ready; run tor tls first\n");
            putString("  Circuits: blocked fail-closed\n");
            return;
        }
        putString("  TLS: initial ServerHello proof exists\n");
        putString("  Circuits: not implemented yet\n");
        putString("  Missing native Tor pieces:\n");
        putString("    1. Complete TLS handshake and key schedule\n");
        putString("    2. Encrypted TLS record read/write\n");
        putString("    3. Tor link cells: VERSIONS, CERTS, AUTH_CHALLENGE, NETINFO\n");
        putString("    4. ntor CREATE2/CREATED2 circuit handshake\n");
        putString("    5. RELAY_BEGIN/CONNECTED/DATA/END stream cells\n");
        putString("  Safe result: no onion app traffic is allowed yet\n");
        return;
    }

    bool wantConsensus = (strcmp(firstArg, "bootstrap") == 0 || strcmp(firstArg, "consensus") == 0);
    if (!wantConsensus) {
        putString("  Usage:\n");
        putString("    tor status\n");
        putString("    tor bootstrap\n");
        putString("    tor consensus\n");
        putString("    tor tls\n");
        putString("    tor crypto\n");
        putString("    tor circuit\n");
        return;
    }

    uint32_t authorityIp = 0;
    network->parseIp("128.31.0.39", &authorityIp); // moria1 Tor directory authority
    static char response[8192];
    static const char request[] =
        "GET /tor/status-vote/current/consensus HTTP/1.0\r\n"
        "Host: 128.31.0.39\r\n"
        "Connection: close\r\n\r\n";

    putString("  Connecting to Tor directory authority moria1 128.31.0.39:9131...\n");
    bool ok = network->tcpRequestText(authorityIp, 9131, request, response, sizeof(response));
    if (!ok) {
        torDirectoryReachable = false;
        putString("  Directory: TCP/HTTP request failed\n");
        putString("  Status: not connected to Tor yet\n");
        return;
    }

    if (textContains(response, "network-status-version") || textContains(response, "HTTP/1.0 200") || textContains(response, "HTTP/1.1 200")) {
        TorConsensusSummary summary;
        parseTorConsensus(response, &summary);

        torDirectoryReachable = true;
        torRelayCount = summary.relays;
        torGuardCount = summary.guards;
        torExitCount = summary.exits;
        torFastCount = summary.fast;
        torStableCount = summary.stable;
        torRunningCount = summary.running;
        torValidCount = summary.valid;
        torUsableGuardCount = summary.usableGuards;
        torSelectedOrPort = summary.selectedOrPort;
        torSelectedHasFast = summary.selectedFast;
        torSelectedHasStable = summary.selectedStable;
        torSelectedHasRunning = summary.selectedRunning;
        torSelectedHasValid = summary.selectedValid;
        torSelectedHasExit = summary.selectedExit;
        int i = 0;
        for (; summary.selectedNickname[i] && i < 31; i++) torSelectedNickname[i] = summary.selectedNickname[i];
        torSelectedNickname[i] = '\0';
        i = 0;
        for (; summary.selectedIp[i] && i < 15; i++) torSelectedIp[i] = summary.selectedIp[i];
        torSelectedIp[i] = '\0';

        static char num[16];
        putString("  Directory: reachable; consensus response started\n");
        putString("  Tor consensus parsed\n");
        u32ToDec(torRelayCount, num, sizeof(num)); putString("  Relays: "); putString(num); putString("\n");
        u32ToDec(torGuardCount, num, sizeof(num)); putString("  Guards: "); putString(num); putString("\n");
        u32ToDec(torExitCount, num, sizeof(num)); putString("  Exits: "); putString(num); putString("\n");
        u32ToDec(torFastCount, num, sizeof(num)); putString("  Fast: "); putString(num); putString("\n");
        u32ToDec(torStableCount, num, sizeof(num)); putString("  Stable: "); putString(num); putString("\n");
        u32ToDec(torRunningCount, num, sizeof(num)); putString("  Running: "); putString(num); putString("\n");
        u32ToDec(torValidCount, num, sizeof(num)); putString("  Valid: "); putString(num); putString("\n");
        u32ToDec(torUsableGuardCount, num, sizeof(num)); putString("  Usable guards: "); putString(num); putString("\n");
        if (torSelectedNickname[0]) {
            putString("  Selected guard candidate:\n");
            putString("    nickname: "); putString(torSelectedNickname); putString("\n");
            putString("    ip: "); putString(torSelectedIp); putString("\n");
            u32ToDec(torSelectedOrPort, num, sizeof(num)); putString("    orport: "); putString(num); putString("\n");
            putString("    flags: Guard");
            if (torSelectedHasFast) putString(" Fast");
            if (torSelectedHasStable) putString(" Stable");
            if (torSelectedHasRunning) putString(" Running");
            if (torSelectedHasValid) putString(" Valid");
            if (torSelectedHasExit) putString(" Exit");
            putString("\n");
        } else {
            putString("  Selected guard candidate: none in received sample\n");
        }
        putString("  Tor link: directory consensus proof OK\n");
        putString("  Next: TLS connection to selected ORPort\n");
    } else {
        torDirectoryReachable = false;
        torRelayCount = 0;
        torGuardCount = 0;
        torExitCount = 0;
        torFastCount = 0;
        torStableCount = 0;
        torRunningCount = 0;
        torValidCount = 0;
        torUsableGuardCount = 0;
        torSelectedNickname[0] = '\0';
        torSelectedIp[0] = '\0';
        torSelectedOrPort = 0;
        putString("  Directory: connected but response was not a consensus\n");
    }

    torCircuitsReady = false;
    putString("  Circuits: not implemented yet\n");
    putString("  Safe result: onion apps still blocked until TLS, Tor cells, ntor, circuits, and streams exist\n");
}

void Terminal::cmdSecureChat(const char* args) {
    putString("\nSecureChat onion-only control\n");
    putString("  Policy: onion-only, no clearnet fallback\n");
    putString("  App E2E crypto: required before message send\n");

    if (!network) {
        putString("  Network: unavailable\n");
        putString("  Status: blocked\n");
        return;
    }

    const NetworkInfo& info = network->getInfo();
    putString("  Network: ");
    if (info.rtl8139Present && info.rxEnabled) {
        putString("ready\n");
    } else {
        putString("not ready\n");
        putString("  Status: blocked until Ethernet is online\n");
        return;
    }

    if (args[0] == '\0' || strcmp(args, "status") == 0) {
        putString("  Tor directory: ");
        putString(torDirectoryReachable ? "reachable\n" : "not checked/reachable yet\n");
        putString("  Tor TLS: ");
        putString(torTlsReady ? "server handshake record seen\n" : "not ready\n");
        putString("  Tor circuits: ");
        putString(torCircuitsReady ? "ready\n" : "not implemented yet\n");
        putString("  Status: blocked until native Tor circuits/streams are available\n");
        putString("  Next: run tor consensus, tor tls, then implement TLS keys, Tor cells, ntor, circuits, streams\n");
        return;
    }

    if (strcmp(args, "start") == 0) {
        putString("  Checking Tor bootstrap first...\n");
        if (!torDirectoryReachable) {
            cmdTor("bootstrap");
        }
        putString("  Start request denied safely\n");
        putString("  Reason: Tor circuits/streams are not implemented yet\n");
        putString("  No direct TCP/DNS chat fallback was attempted\n");
        return;
    }

    putString("  Usage:\n");
    putString("    securechat status\n");
    putString("    securechat start\n");
}
