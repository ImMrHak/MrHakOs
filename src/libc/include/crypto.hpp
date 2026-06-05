#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <stdint.h>

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
bool crypto_self_test();

#endif // CRYPTO_HPP
