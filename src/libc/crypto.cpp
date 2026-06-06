#include <crypto.hpp>
#include <string.hpp>

// ===========================================================================
// Small endian helpers
// ===========================================================================
static uint32_t rotr32(uint32_t x, uint8_t n) { return (x >> n) | (x << (32 - n)); }
static uint32_t rotl32(uint32_t x, uint8_t n) { return (x << n) | (x >> (32 - n)); }

static uint32_t rdbe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
static void wrbe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);  p[3] = static_cast<uint8_t>(v);
}
static void wrbe64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = static_cast<uint8_t>(v >> (56 - i * 8));
}

// ===========================================================================
// SHA-256
// ===========================================================================
static uint32_t ch32(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static uint32_t maj32(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static uint32_t bsig0(uint32_t x) { return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22); }
static uint32_t bsig1(uint32_t x) { return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25); }
static uint32_t ssig0(uint32_t x) { return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3); }
static uint32_t ssig1(uint32_t x) { return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10); }

static const uint32_t K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static void sha256_transform(Sha256Ctx* ctx, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = rdbe32(block + i * 4);
    for (int i = 16; i < 64; i++) w[i] = ssig1(w[i - 2]) + w[i - 7] + ssig0(w[i - 15]) + w[i - 16];
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + bsig1(e) + ch32(e, f, g) + K256[i] + w[i];
        uint32_t t2 = bsig0(a) + maj32(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(Sha256Ctx* ctx) {
    if (!ctx) return;
    ctx->state[0]=0x6a09e667u; ctx->state[1]=0xbb67ae85u; ctx->state[2]=0x3c6ef372u; ctx->state[3]=0xa54ff53au;
    ctx->state[4]=0x510e527fu; ctx->state[5]=0x9b05688cu; ctx->state[6]=0x1f83d9abu; ctx->state[7]=0x5be0cd19u;
    ctx->bitLen = 0; ctx->bufferLen = 0;
}

void sha256_update(Sha256Ctx* ctx, const uint8_t* data, uint32_t len) {
    if (!ctx || (!data && len)) return;
    for (uint32_t i = 0; i < len; i++) {
        ctx->buffer[ctx->bufferLen++] = data[i];
        if (ctx->bufferLen == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->bitLen += 512u;
            ctx->bufferLen = 0;
        }
    }
}

void sha256_final(Sha256Ctx* ctx, uint8_t out[32]) {
    if (!ctx || !out) return;
    uint64_t totalBits = ctx->bitLen + static_cast<uint64_t>(ctx->bufferLen) * 8u;
    ctx->buffer[ctx->bufferLen++] = 0x80;
    if (ctx->bufferLen > 56) {
        while (ctx->bufferLen < 64) ctx->buffer[ctx->bufferLen++] = 0;
        sha256_transform(ctx, ctx->buffer);
        ctx->bufferLen = 0;
    }
    while (ctx->bufferLen < 56) ctx->buffer[ctx->bufferLen++] = 0;
    wrbe64(ctx->buffer + 56, totalBits);
    sha256_transform(ctx, ctx->buffer);
    for (int i = 0; i < 8; i++) wrbe32(out + i * 4, ctx->state[i]);
}

void sha256(const uint8_t* data, uint32_t len, uint8_t out[32]) {
    Sha256Ctx ctx; sha256_init(&ctx); sha256_update(&ctx, data, len); sha256_final(&ctx, out);
}

void hmac_sha256(const uint8_t* key, uint32_t keyLen, const uint8_t* data, uint32_t dataLen, uint8_t out[32]) {
    uint8_t k0[64]; uint8_t kh[32];
    memset(k0, 0, sizeof(k0));
    if (keyLen > 64) { sha256(key, keyLen, kh); memcpy(k0, kh, 32); }
    else if (keyLen && key) { memcpy(k0, key, keyLen); }
    uint8_t ipad[64]; uint8_t opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = static_cast<uint8_t>(k0[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(k0[i] ^ 0x5c);
    }
    uint8_t inner[32]; Sha256Ctx ctx;
    sha256_init(&ctx); sha256_update(&ctx, ipad, 64); sha256_update(&ctx, data, dataLen); sha256_final(&ctx, inner);
    sha256_init(&ctx); sha256_update(&ctx, opad, 64); sha256_update(&ctx, inner, 32); sha256_final(&ctx, out);
}

// ===========================================================================
// SHA-1
// ===========================================================================
static void sha1_transform(Sha1Ctx* ctx, const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = rdbe32(block + i * 4);
    for (int i = 16; i < 80; i++) w[i] = rotl32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3], e = ctx->state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d);           k = 0x5a827999u; }
        else if (i < 40) { f = b ^ c ^ d;                      k = 0x6ed9eba1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);    k = 0x8f1bbcdcu; }
        else             { f = b ^ c ^ d;                      k = 0xca62c1d6u; }
        uint32_t t = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotl32(b, 30); b = a; a = t;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d; ctx->state[4] += e;
}

void sha1_init(Sha1Ctx* ctx) {
    if (!ctx) return;
    ctx->state[0]=0x67452301u; ctx->state[1]=0xEFCDAB89u; ctx->state[2]=0x98BADCFEu;
    ctx->state[3]=0x10325476u; ctx->state[4]=0xC3D2E1F0u;
    ctx->bitLen = 0; ctx->bufferLen = 0;
}

void sha1_update(Sha1Ctx* ctx, const uint8_t* data, uint32_t len) {
    if (!ctx || (!data && len)) return;
    for (uint32_t i = 0; i < len; i++) {
        ctx->buffer[ctx->bufferLen++] = data[i];
        if (ctx->bufferLen == 64) { sha1_transform(ctx, ctx->buffer); ctx->bitLen += 512u; ctx->bufferLen = 0; }
    }
}

void sha1_final(Sha1Ctx* ctx, uint8_t out[20]) {
    if (!ctx || !out) return;
    uint64_t totalBits = ctx->bitLen + static_cast<uint64_t>(ctx->bufferLen) * 8u;
    ctx->buffer[ctx->bufferLen++] = 0x80;
    if (ctx->bufferLen > 56) {
        while (ctx->bufferLen < 64) ctx->buffer[ctx->bufferLen++] = 0;
        sha1_transform(ctx, ctx->buffer);
        ctx->bufferLen = 0;
    }
    while (ctx->bufferLen < 56) ctx->buffer[ctx->bufferLen++] = 0;
    wrbe64(ctx->buffer + 56, totalBits);
    sha1_transform(ctx, ctx->buffer);
    for (int i = 0; i < 5; i++) wrbe32(out + i * 4, ctx->state[i]);
}

void sha1(const uint8_t* data, uint32_t len, uint8_t out[20]) {
    Sha1Ctx ctx; sha1_init(&ctx); sha1_update(&ctx, data, len); sha1_final(&ctx, out);
}

// ===========================================================================
// RNG: RDRAND (when present) + RDTSC, folded through a SHA-256 hash DRBG.
// ===========================================================================
static inline uint64_t cpu_rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static bool cpu_has_rdrand() {
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1u), "c"(0u));
    (void)eax; (void)ebx; (void)edx;
    return (ecx & (1u << 30)) != 0;
}

static bool cpu_rdrand32(uint32_t* out) {
    uint32_t val = 0; uint8_t ok = 0;
    // rdrand eax ; setc ok   (encode rdrand by bytes so old assemblers accept it)
    asm volatile(".byte 0x0f,0xc7,0xf0\n\tsetc %1" : "=a"(val), "=qm"(ok) : : "cc");
    if (ok) { *out = val; return true; }
    return false;
}

static uint8_t g_drbg[32];
static bool g_drbgSeeded = false;
static uint64_t g_drbgCtr = 0;

static void rng_seed() {
    Sha256Ctx c; sha256_init(&c);
    sha256_update(&c, g_drbg, 32);
    bool rd = cpu_has_rdrand();
    for (int i = 0; i < 24; i++) {
        uint64_t t = cpu_rdtsc();
        sha256_update(&c, reinterpret_cast<const uint8_t*>(&t), 8);
        if (rd) { uint32_t r; if (cpu_rdrand32(&r)) sha256_update(&c, reinterpret_cast<const uint8_t*>(&r), 4); }
    }
    uint64_t ctr = g_drbgCtr++;
    sha256_update(&c, reinterpret_cast<const uint8_t*>(&ctr), 8);
    sha256_final(&c, g_drbg);
    g_drbgSeeded = true;
}

void rng_bytes(uint8_t* out, uint32_t len) {
    if (!out) return;
    if (!g_drbgSeeded) rng_seed();
    uint32_t off = 0;
    while (off < len) {
        Sha256Ctx c; sha256_init(&c);
        sha256_update(&c, g_drbg, 32);
        uint64_t ctr = g_drbgCtr++;
        uint64_t t = cpu_rdtsc();
        sha256_update(&c, reinterpret_cast<const uint8_t*>(&ctr), 8);
        sha256_update(&c, reinterpret_cast<const uint8_t*>(&t), 8);
        uint8_t block[32]; sha256_final(&c, block);
        uint32_t n = len - off; if (n > 32) n = 32;
        for (uint32_t i = 0; i < n; i++) out[off + i] = block[i];
        off += n;
        // Ratchet the pool forward so output never reveals state.
        Sha256Ctx u; sha256_init(&u);
        sha256_update(&u, g_drbg, 32);
        sha256_update(&u, block, 32);
        sha256_final(&u, g_drbg);
    }
}

// ===========================================================================
// Curve25519 / X25519 — adapted from the public-domain TweetNaCl. Uses only
// int64_t arithmetic so it is correct on both 32-bit and 64-bit targets.
// ===========================================================================
typedef int64_t gf[16];
static const gf gf_121665 = {0xDB41, 1};

static void car25519(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}
static void sel25519(gf p, gf q, int b) {
    int64_t c = ~(static_cast<int64_t>(b) - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t; q[i] ^= t;
    }
}
static void pack25519(uint8_t* o, const gf n) {
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = static_cast<uint8_t>(t[i] & 0xff);
        o[2 * i + 1] = static_cast<uint8_t>(t[i] >> 8);
    }
}
static void unpack25519(gf o, const uint8_t* n) {
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + (static_cast<int64_t>(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}
static void fadd(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fsub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
static void fmul(gf o, const gf a, const gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++) for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o); car25519(o);
}
static void fsqr(gf o, const gf a) { fmul(o, a, a); }
static void finv(gf o, const gf i) {
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) { fsqr(c, c); if (a != 2 && a != 4) fmul(c, c, i); }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

void x25519_scalarmult(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t z[32];
    int64_t x[80];
    gf a, b, c, d, e, f;
    for (int i = 0; i < 31; i++) z[i] = scalar[i];
    z[31] = (scalar[31] & 127) | 64;
    z[0] &= 248;
    unpack25519(x, point);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        int64_t r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r); sel25519(c, d, r);
        fadd(e, a, c); fsub(a, a, c); fadd(c, b, d); fsub(b, b, d);
        fsqr(d, e); fsqr(f, a);
        fmul(a, c, a); fmul(c, b, e);
        fadd(e, a, c); fsub(a, a, c);
        fsqr(b, a); fsub(c, d, f);
        fmul(a, c, gf_121665); fadd(a, a, d);
        fmul(c, c, a); fmul(a, d, f);
        fmul(d, b, x); fsqr(b, e);
        sel25519(a, b, r); sel25519(c, d, r);
    }
    for (int i = 0; i < 16; i++) { x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i]; }
    finv(x + 32, x + 32);
    fmul(x + 16, x + 16, x + 32);
    pack25519(out, x + 16);
}

void x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    static const uint8_t base[32] = {9};
    x25519_scalarmult(out, scalar, base);
}

// ===========================================================================
// AES-128 (encryption-only forward cipher)
// ===========================================================================
static const uint8_t AES_SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static const uint8_t AES_RCON[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

void aes128_init(Aes128* ctx, const uint8_t key[16]) {
    uint8_t* rk = ctx->roundKey;
    for (int i = 0; i < 16; i++) rk[i] = key[i];
    for (int i = 16; i < 176; i += 4) {
        uint8_t t0 = rk[i - 4], t1 = rk[i - 3], t2 = rk[i - 2], t3 = rk[i - 1];
        if ((i & 15) == 0) {
            uint8_t s0 = AES_SBOX[t1], s1 = AES_SBOX[t2], s2 = AES_SBOX[t3], s3 = AES_SBOX[t0];
            t0 = static_cast<uint8_t>(s0 ^ AES_RCON[(i / 16) - 1]); t1 = s1; t2 = s2; t3 = s3;
        }
        rk[i]     = static_cast<uint8_t>(rk[i - 16] ^ t0);
        rk[i + 1] = static_cast<uint8_t>(rk[i - 15] ^ t1);
        rk[i + 2] = static_cast<uint8_t>(rk[i - 14] ^ t2);
        rk[i + 3] = static_cast<uint8_t>(rk[i - 13] ^ t3);
    }
}

static uint8_t xtime(uint8_t x) { return static_cast<uint8_t>((x << 1) ^ ((x >> 7) * 0x1b)); }

void aes128_encrypt_block(const Aes128* ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    const uint8_t* rk = ctx->roundKey;
    for (int i = 0; i < 16; i++) s[i] = static_cast<uint8_t>(in[i] ^ rk[i]);
    for (int round = 1; round <= 10; round++) {
        // SubBytes
        for (int i = 0; i < 16; i++) s[i] = AES_SBOX[s[i]];
        // ShiftRows (state is column-major: byte index = col*4 + row)
        uint8_t t[16];
        t[0]=s[0];  t[4]=s[4];  t[8]=s[8];   t[12]=s[12];
        t[1]=s[5];  t[5]=s[9];  t[9]=s[13];  t[13]=s[1];
        t[2]=s[10]; t[6]=s[14]; t[10]=s[2];  t[14]=s[6];
        t[3]=s[15]; t[7]=s[3];  t[11]=s[7];  t[15]=s[11];
        for (int i = 0; i < 16; i++) s[i] = t[i];
        // MixColumns (skip on final round)
        if (round != 10) {
            for (int c = 0; c < 4; c++) {
                uint8_t* col = s + c * 4;
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                uint8_t h = static_cast<uint8_t>(a0 ^ a1 ^ a2 ^ a3);
                col[0] = static_cast<uint8_t>(a0 ^ h ^ xtime(static_cast<uint8_t>(a0 ^ a1)));
                col[1] = static_cast<uint8_t>(a1 ^ h ^ xtime(static_cast<uint8_t>(a1 ^ a2)));
                col[2] = static_cast<uint8_t>(a2 ^ h ^ xtime(static_cast<uint8_t>(a2 ^ a3)));
                col[3] = static_cast<uint8_t>(a3 ^ h ^ xtime(static_cast<uint8_t>(a3 ^ a0)));
            }
        }
        // AddRoundKey
        const uint8_t* rkr = rk + round * 16;
        for (int i = 0; i < 16; i++) s[i] = static_cast<uint8_t>(s[i] ^ rkr[i]);
    }
    for (int i = 0; i < 16; i++) out[i] = s[i];
}

// ===========================================================================
// Streaming AES-128-CTR (full 128-bit big-endian counter, keystream retained)
// ===========================================================================
static void ctr_incr_full(uint8_t c[16]) {
    for (int i = 15; i >= 0; i--) { if (++c[i] != 0) break; }
}

void aes128_ctr_init(AesCtrState* st, const uint8_t key[16], const uint8_t iv[16]) {
    aes128_init(&st->aes, key);
    for (int i = 0; i < 16; i++) st->counter[i] = iv[i];
    st->pos = 16; // force a fresh keystream block on first use
}

void aes128_ctr_crypt(AesCtrState* st, const uint8_t* in, uint8_t* out, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (st->pos == 16) {
            aes128_encrypt_block(&st->aes, st->counter, st->keystream);
            ctr_incr_full(st->counter);
            st->pos = 0;
        }
        out[i] = static_cast<uint8_t>(in[i] ^ st->keystream[st->pos++]);
    }
}

// ===========================================================================
// GHASH + AES-128-GCM (12-byte IV)
// ===========================================================================
static void ghash_mul(uint8_t X[16], const uint8_t H[16]) {
    uint8_t Z[16]; uint8_t V[16];
    for (int i = 0; i < 16; i++) { Z[i] = 0; V[i] = H[i]; }
    for (int i = 0; i < 128; i++) {
        int byte = i >> 3, bit = 7 - (i & 7);
        if ((X[byte] >> bit) & 1) { for (int j = 0; j < 16; j++) Z[j] ^= V[j]; }
        uint8_t lsb = static_cast<uint8_t>(V[15] & 1);
        for (int j = 15; j > 0; j--) V[j] = static_cast<uint8_t>((V[j] >> 1) | ((V[j - 1] & 1) << 7));
        V[0] >>= 1;
        if (lsb) V[0] ^= 0xe1;
    }
    for (int i = 0; i < 16; i++) X[i] = Z[i];
}

static void ghash_data(uint8_t S[16], const uint8_t H[16], const uint8_t* d, uint32_t len) {
    uint32_t off = 0;
    while (off < len) {
        uint32_t n = len - off; if (n > 16) n = 16;
        for (uint32_t j = 0; j < n; j++) S[j] ^= d[off + j];
        ghash_mul(S, H);
        off += n;
    }
}

static void gcm_core(const uint8_t key[16], const uint8_t iv[12],
                     const uint8_t* aad, uint32_t aadLen,
                     const uint8_t* inData, uint32_t dataLen, uint8_t* outData,
                     uint8_t tag[16]) {
    Aes128 a; aes128_init(&a, key);
    uint8_t H[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    aes128_encrypt_block(&a, H, H);
    uint8_t J0[16];
    for (int i = 0; i < 12; i++) J0[i] = iv[i];
    J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;

    // CTR over the data, starting at inc32(J0).
    uint8_t ctr[16];
    for (int i = 0; i < 16; i++) ctr[i] = J0[i];
    uint32_t off = 0;
    while (off < dataLen) {
        for (int i = 15; i >= 12; i--) { if (++ctr[i] != 0) break; }
        uint8_t ks[16]; aes128_encrypt_block(&a, ctr, ks);
        uint32_t n = dataLen - off; if (n > 16) n = 16;
        for (uint32_t j = 0; j < n; j++) outData[off + j] = static_cast<uint8_t>(inData[off + j] ^ ks[j]);
        off += n;
    }

    uint8_t S[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    ghash_data(S, H, aad, aadLen);
    ghash_data(S, H, outData, dataLen);
    uint8_t lenBlock[16];
    wrbe64(lenBlock, static_cast<uint64_t>(aadLen) * 8u);
    wrbe64(lenBlock + 8, static_cast<uint64_t>(dataLen) * 8u);
    for (int i = 0; i < 16; i++) S[i] ^= lenBlock[i];
    ghash_mul(S, H);

    uint8_t EJ0[16]; aes128_encrypt_block(&a, J0, EJ0);
    for (int i = 0; i < 16; i++) tag[i] = static_cast<uint8_t>(EJ0[i] ^ S[i]);
}

void aes128_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                        const uint8_t* aad, uint32_t aadLen,
                        const uint8_t* pt, uint32_t ptLen,
                        uint8_t* ct, uint8_t tag[16]) {
    gcm_core(key, iv, aad, aadLen, pt, ptLen, ct, tag);
}

bool aes128_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                        const uint8_t* aad, uint32_t aadLen,
                        const uint8_t* ct, uint32_t ctLen,
                        const uint8_t tag[16], uint8_t* pt) {
    // Recompute the tag from the ciphertext+AAD before producing plaintext.
    Aes128 a; aes128_init(&a, key);
    uint8_t H[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    aes128_encrypt_block(&a, H, H);
    uint8_t J0[16];
    for (int i = 0; i < 12; i++) J0[i] = iv[i];
    J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;

    uint8_t S[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    ghash_data(S, H, aad, aadLen);
    ghash_data(S, H, ct, ctLen);
    uint8_t lenBlock[16];
    wrbe64(lenBlock, static_cast<uint64_t>(aadLen) * 8u);
    wrbe64(lenBlock + 8, static_cast<uint64_t>(ctLen) * 8u);
    for (int i = 0; i < 16; i++) S[i] ^= lenBlock[i];
    ghash_mul(S, H);
    uint8_t EJ0[16]; aes128_encrypt_block(&a, J0, EJ0);
    uint8_t expect[16];
    for (int i = 0; i < 16; i++) expect[i] = static_cast<uint8_t>(EJ0[i] ^ S[i]);
    if (!crypto_equal(expect, tag, 16)) return false;

    // Tag OK -> decrypt with CTR from inc32(J0).
    uint8_t ctr[16];
    for (int i = 0; i < 16; i++) ctr[i] = J0[i];
    uint32_t off = 0;
    while (off < ctLen) {
        for (int i = 15; i >= 12; i--) { if (++ctr[i] != 0) break; }
        uint8_t ks[16]; aes128_encrypt_block(&a, ctr, ks);
        uint32_t n = ctLen - off; if (n > 16) n = 16;
        for (uint32_t j = 0; j < n; j++) pt[off + j] = static_cast<uint8_t>(ct[off + j] ^ ks[j]);
        off += n;
    }
    return true;
}

// ===========================================================================
// HKDF-SHA256 and TLS 1.2 PRF
// ===========================================================================
void hkdf_sha256_extract(const uint8_t* salt, uint32_t saltLen,
                         const uint8_t* ikm, uint32_t ikmLen, uint8_t prk[32]) {
    uint8_t zeroSalt[32];
    if (!salt || saltLen == 0) { memset(zeroSalt, 0, 32); salt = zeroSalt; saltLen = 32; }
    hmac_sha256(salt, saltLen, ikm, ikmLen, prk);
}

void hkdf_sha256_expand(const uint8_t prk[32], const uint8_t* info, uint32_t infoLen,
                        uint8_t* out, uint32_t outLen) {
    uint8_t t[32];
    uint32_t tLen = 0;
    uint32_t off = 0;
    uint8_t counter = 1;
    while (off < outLen) {
        // T(i) = HMAC(PRK, T(i-1) | info | i)
        uint8_t buf[32 + 512 + 1];
        uint32_t p = 0;
        if (tLen) { for (uint32_t i = 0; i < tLen; i++) buf[p++] = t[i]; }
        uint32_t ilen = infoLen; if (ilen > 512) ilen = 512;
        for (uint32_t i = 0; i < ilen; i++) buf[p++] = info[i];
        buf[p++] = counter;
        hmac_sha256(prk, 32, buf, p, t);
        tLen = 32;
        uint32_t n = outLen - off; if (n > 32) n = 32;
        for (uint32_t i = 0; i < n; i++) out[off + i] = t[i];
        off += n;
        counter++;
    }
}

void tls12_prf_sha256(const uint8_t* secret, uint32_t secretLen,
                      const char* label, const uint8_t* seed, uint32_t seedLen,
                      uint8_t* out, uint32_t outLen) {
    // P_SHA256(secret, label || seed)
    uint32_t labelLen = 0;
    while (label[labelLen]) labelLen++;
    uint8_t ls[64 + 256];
    uint32_t lsLen = 0;
    for (uint32_t i = 0; i < labelLen; i++) ls[lsLen++] = static_cast<uint8_t>(label[i]);
    uint32_t sl = seedLen; if (sl > 256) sl = 256;
    for (uint32_t i = 0; i < sl; i++) ls[lsLen++] = seed[i];

    uint8_t a[32];
    hmac_sha256(secret, secretLen, ls, lsLen, a); // A(1) = HMAC(secret, seed')
    uint32_t off = 0;
    while (off < outLen) {
        uint8_t buf[32 + 64 + 256];
        uint32_t p = 0;
        for (int i = 0; i < 32; i++) buf[p++] = a[i];
        for (uint32_t i = 0; i < lsLen; i++) buf[p++] = ls[i];
        uint8_t block[32];
        hmac_sha256(secret, secretLen, buf, p, block);
        uint32_t n = outLen - off; if (n > 32) n = 32;
        for (uint32_t i = 0; i < n; i++) out[off + i] = block[i];
        off += n;
        hmac_sha256(secret, secretLen, a, 32, a); // A(i+1) = HMAC(secret, A(i))
    }
}

bool crypto_equal(const uint8_t* a, const uint8_t* b, uint32_t len) {
    uint8_t diff = 0;
    for (uint32_t i = 0; i < len; i++) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

// ===========================================================================
// Self-tests with published known-answer vectors
// ===========================================================================
static bool eqn(const uint8_t* a, const uint8_t* b, uint32_t n) { return crypto_equal(a, b, n); }

bool crypto_self_test() {
    // --- SHA-256("abc") ---
    static const uint8_t shaAbc[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    uint8_t d32[32];
    sha256(reinterpret_cast<const uint8_t*>("abc"), 3, d32);
    if (!eqn(d32, shaAbc, 32)) return false;

    // --- HMAC-SHA256 (RFC 4231 test 1) ---
    static const uint8_t hmacExpected[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };
    uint8_t key20[20]; for (int i = 0; i < 20; i++) key20[i] = 0x0b;
    hmac_sha256(key20, 20, reinterpret_cast<const uint8_t*>("Hi There"), 8, d32);
    if (!eqn(d32, hmacExpected, 32)) return false;

    // --- SHA-1("abc") ---
    static const uint8_t sha1Abc[20] = {
        0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
        0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d
    };
    uint8_t d20[20];
    sha1(reinterpret_cast<const uint8_t*>("abc"), 3, d20);
    if (!eqn(d20, sha1Abc, 20)) return false;

    // --- AES-128 (FIPS-197 example) ---
    static const uint8_t aesKey[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    static const uint8_t aesPt[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
    };
    static const uint8_t aesCt[16] = {
        0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a
    };
    Aes128 aes; aes128_init(&aes, aesKey);
    uint8_t aesOut[16]; aes128_encrypt_block(&aes, aesPt, aesOut);
    if (!eqn(aesOut, aesCt, 16)) return false;

    // --- AES-128-GCM (NIST GCM spec, Test Case 3: no AAD) ---
    static const uint8_t gK[16] = {
        0xfe,0xff,0xe9,0x92,0x86,0x65,0x73,0x1c,0x6d,0x6a,0x8f,0x94,0x67,0x30,0x83,0x08
    };
    static const uint8_t gIV[12] = {
        0xca,0xfe,0xba,0xbe,0xfa,0xce,0xdb,0xad,0xde,0xca,0xf8,0x88
    };
    static const uint8_t gP[64] = {
        0xd9,0x31,0x32,0x25,0xf8,0x84,0x06,0xe5,0xa5,0x59,0x09,0xc5,0xaf,0xf5,0x26,0x9a,
        0x86,0xa7,0xa9,0x53,0x15,0x34,0xf7,0xda,0x2e,0x4c,0x30,0x3d,0x8a,0x31,0x8a,0x72,
        0x1c,0x3c,0x0c,0x95,0x95,0x68,0x09,0x53,0x2f,0xcf,0x0e,0x24,0x49,0xa6,0xb5,0x25,
        0xb1,0x6a,0xed,0xf5,0xaa,0x0d,0xe6,0x57,0xba,0x63,0x7b,0x39,0x1a,0xaf,0xd2,0x55
    };
    static const uint8_t gC[64] = {
        0x42,0x83,0x1e,0xc2,0x21,0x77,0x74,0x24,0x4b,0x72,0x21,0xb7,0x84,0xd0,0xd4,0x9c,
        0xe3,0xaa,0x21,0x2f,0x2c,0x02,0xa4,0xe0,0x35,0xc1,0x7e,0x23,0x29,0xac,0xa1,0x2e,
        0x21,0xd5,0x14,0xb2,0x54,0x66,0x93,0x1c,0x7d,0x8f,0x6a,0x5a,0xac,0x84,0xaa,0x05,
        0x1b,0xa3,0x0b,0x39,0x6a,0x0a,0xac,0x97,0x3d,0x58,0xe0,0x91,0x47,0x3f,0x59,0x85
    };
    static const uint8_t gT[16] = {
        0x4d,0x5c,0x2a,0xf3,0x27,0xcd,0x64,0xa6,0x2c,0xf3,0x5a,0xbd,0x2b,0xa6,0xfa,0xb4
    };
    uint8_t gOut[64]; uint8_t gTag[16];
    aes128_gcm_encrypt(gK, gIV, nullptr, 0, gP, 64, gOut, gTag);
    if (!eqn(gOut, gC, 64)) return false;
    if (!eqn(gTag, gT, 16)) return false;
    // round-trip including an AAD to exercise the AAD path + decrypt verify
    uint8_t gBack[64];
    static const uint8_t aad[20] = {
        0xfe,0xed,0xfa,0xce,0xde,0xad,0xbe,0xef,0xfe,0xed,0xfa,0xce,0xde,0xad,0xbe,0xef,0xab,0xad,0xda,0xd2
    };
    aes128_gcm_encrypt(gK, gIV, aad, 20, gP, 64, gOut, gTag);
    if (!aes128_gcm_decrypt(gK, gIV, aad, 20, gOut, 64, gTag, gBack)) return false;
    if (!eqn(gBack, gP, 64)) return false;
    gTag[0] ^= 0x80; // corrupted tag must fail
    if (aes128_gcm_decrypt(gK, gIV, aad, 20, gOut, 64, gTag, gBack)) return false;

    // --- X25519 (RFC 7748, section 5.2, first vector) ---
    static const uint8_t xScalar[32] = {
        0xa5,0x46,0xe3,0x6b,0xf0,0x52,0x7c,0x9d,0x3b,0x16,0x15,0x4b,0x82,0x46,0x5e,0xdd,
        0x62,0x14,0x4c,0x0a,0xc1,0xfc,0x5a,0x18,0x50,0x6a,0x22,0x44,0xba,0x44,0x9a,0xc4
    };
    static const uint8_t xPoint[32] = {
        0xe6,0xdb,0x68,0x67,0x58,0x30,0x30,0xdb,0x35,0x94,0xc1,0xa4,0x24,0xb1,0x5f,0x7c,
        0x72,0x66,0x24,0xec,0x26,0xb3,0x35,0x3b,0x10,0xa9,0x03,0xa6,0xd0,0xab,0x1c,0x4c
    };
    static const uint8_t xOut[32] = {
        0xc3,0xda,0x55,0x37,0x9d,0xe9,0xc6,0x90,0x8e,0x94,0xea,0x4d,0xf2,0x8d,0x08,0x4f,
        0x32,0xec,0xcf,0x03,0x49,0x1c,0x71,0xf7,0x54,0xb4,0x07,0x55,0x77,0xa2,0x85,0x52
    };
    uint8_t xr[32];
    x25519_scalarmult(xr, xScalar, xPoint);
    if (!eqn(xr, xOut, 32)) return false;

    // --- HKDF-SHA256 (RFC 5869, Test Case 1) ---
    static const uint8_t ikm[22] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b
    };
    static const uint8_t salt[13] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c};
    static const uint8_t info[10] = {0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9};
    static const uint8_t okmExp[42] = {
        0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a,
        0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,0xec,0xc4,0xc5,0xbf,
        0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65
    };
    uint8_t prk[32]; uint8_t okm[42];
    hkdf_sha256_extract(salt, 13, ikm, 22, prk);
    hkdf_sha256_expand(prk, info, 10, okm, 42);
    if (!eqn(okm, okmExp, 42)) return false;

    return true;
}
