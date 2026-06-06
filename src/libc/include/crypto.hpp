#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <stdint.h>

// ---------------------------------------------------------------------------
// SHA-256 + HMAC-SHA256 (used by TLS PRF, HKDF and the ntor handshake).
// ---------------------------------------------------------------------------
struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitLen;
    uint8_t buffer[64];
    uint32_t bufferLen;
};

void sha256_init(Sha256Ctx* ctx);
void sha256_update(Sha256Ctx* ctx, const uint8_t* data, uint32_t len);
void sha256_final(Sha256Ctx* ctx, uint8_t out[32]);
void sha256(const uint8_t* data, uint32_t len, uint8_t out[32]);
void hmac_sha256(const uint8_t* key, uint32_t keyLen, const uint8_t* data, uint32_t dataLen, uint8_t out[32]);

// ---------------------------------------------------------------------------
// SHA-1 (Tor RELAY-cell running digest is SHA-1).
// ---------------------------------------------------------------------------
struct Sha1Ctx {
    uint32_t state[5];
    uint64_t bitLen;
    uint8_t buffer[64];
    uint32_t bufferLen;
};

void sha1_init(Sha1Ctx* ctx);
void sha1_update(Sha1Ctx* ctx, const uint8_t* data, uint32_t len);
void sha1_final(Sha1Ctx* ctx, uint8_t out[20]);
void sha1(const uint8_t* data, uint32_t len, uint8_t out[20]);

// ---------------------------------------------------------------------------
// Random bytes. Uses RDRAND when the CPU advertises it, always mixed through a
// SHA-256 DRBG that is also seeded from RDTSC samples and a running counter.
// ---------------------------------------------------------------------------
void rng_bytes(uint8_t* out, uint32_t len);

// ---------------------------------------------------------------------------
// Curve25519 / X25519 (RFC 7748). scalar and point are little-endian 32-byte.
// ---------------------------------------------------------------------------
void x25519_scalarmult(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void x25519_base(uint8_t out[32], const uint8_t scalar[32]);

// ---------------------------------------------------------------------------
// AES-128 (encryption only: GCM and CTR both need only the forward cipher).
// ---------------------------------------------------------------------------
struct Aes128 {
    uint8_t roundKey[176]; // 11 * 16
};

void aes128_init(Aes128* ctx, const uint8_t key[16]);
void aes128_encrypt_block(const Aes128* ctx, const uint8_t in[16], uint8_t out[16]);

// Streaming AES-128 in counter mode with a full 128-bit big-endian counter, as
// OpenSSL/Tor use it for RELAY-cell payloads. The keystream position is kept
// across calls so a single logical stream can be split over many cells.
struct AesCtrState {
    Aes128 aes;
    uint8_t counter[16];
    uint8_t keystream[16];
    uint8_t pos; // bytes of keystream[] already consumed (0..16)
};

void aes128_ctr_init(AesCtrState* st, const uint8_t key[16], const uint8_t iv[16]);
void aes128_ctr_crypt(AesCtrState* st, const uint8_t* in, uint8_t* out, uint32_t len);

// ---------------------------------------------------------------------------
// AES-128-GCM with a 12-byte IV (the only shape TLS 1.2 / RFC 5288 needs).
// encrypt writes ctLen ciphertext bytes plus a 16-byte tag.
// decrypt verifies the tag and returns false (without exposing plaintext) on
// mismatch.
// ---------------------------------------------------------------------------
void aes128_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                        const uint8_t* aad, uint32_t aadLen,
                        const uint8_t* pt, uint32_t ptLen,
                        uint8_t* ct, uint8_t tag[16]);
bool aes128_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                        const uint8_t* aad, uint32_t aadLen,
                        const uint8_t* ct, uint32_t ctLen,
                        const uint8_t tag[16], uint8_t* pt);

// ---------------------------------------------------------------------------
// HKDF-SHA256 (RFC 5869) and the TLS 1.2 PRF (RFC 5246, SHA-256 variant).
// ---------------------------------------------------------------------------
void hkdf_sha256_extract(const uint8_t* salt, uint32_t saltLen,
                         const uint8_t* ikm, uint32_t ikmLen, uint8_t prk[32]);
void hkdf_sha256_expand(const uint8_t prk[32], const uint8_t* info, uint32_t infoLen,
                        uint8_t* out, uint32_t outLen);
void tls12_prf_sha256(const uint8_t* secret, uint32_t secretLen,
                      const char* label, const uint8_t* seed, uint32_t seedLen,
                      uint8_t* out, uint32_t outLen);

// Constant-time compare of two equal-length buffers.
bool crypto_equal(const uint8_t* a, const uint8_t* b, uint32_t len);

bool crypto_self_test();

#endif // CRYPTO_HPP
