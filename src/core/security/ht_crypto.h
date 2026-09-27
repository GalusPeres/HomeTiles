#pragma once

// Small, portable cryptographic primitives shared by the Web Admin login and
// the Bridge command channel.
//
// The code has no Arduino, ESP-IDF or mbedTLS dependency, so the exact same
// implementation runs on every S3 and P4 profile and in the host tests under
// tools/tests/core/, which compare it with Node's OpenSSL-backed crypto module.
// It needs no heap and no DMA-capable internal memory: every state lives on the
// caller's stack. The hardware SHA/AES engines are deliberately not used; the
// messages handled here are a few hundred bytes, and the P4 hardware AES path
// may allocate internal DMA bounce buffers that the camera and ESP-Hosted
// transport need.

#include <stddef.h>
#include <stdint.h>

namespace ht_crypto {

constexpr size_t kSha256Size = 32;
constexpr size_t kSha256BlockSize = 64;

struct Sha256 {
  uint32_t state[8];
  uint64_t total_bytes;
  uint8_t buffer[kSha256BlockSize];
  size_t buffer_length;
};

void sha256Init(Sha256& ctx);
void sha256Update(Sha256& ctx, const void* data, size_t length);
void sha256Final(Sha256& ctx, uint8_t out[kSha256Size]);
void sha256(const void* data, size_t length, uint8_t out[kSha256Size]);

struct HmacSha256 {
  Sha256 inner;
  Sha256 outer;
};

void hmacSha256Init(HmacSha256& ctx, const uint8_t* key, size_t key_length);
void hmacSha256Update(HmacSha256& ctx, const void* data, size_t length);
void hmacSha256Final(HmacSha256& ctx, uint8_t out[kSha256Size]);
void hmacSha256(const uint8_t* key, size_t key_length, const void* data,
                size_t length, uint8_t out[kSha256Size]);

// HKDF with HMAC-SHA256 (RFC 5869). out_length is at most 255 * 32 bytes.
void hkdfSha256(const uint8_t* salt, size_t salt_length, const uint8_t* ikm,
                size_t ikm_length, const uint8_t* info, size_t info_length,
                uint8_t* out, size_t out_length);

// ChaCha20-Poly1305 AEAD (RFC 8439) with a 256-bit key and a 96-bit nonce.
// ciphertext may equal plaintext for in-place encryption. open() checks the
// tag in constant time before it decrypts anything and returns false (with
// the output cleared) on any mismatch.
constexpr size_t kAeadKeySize = 32;
constexpr size_t kAeadNonceSize = 12;
constexpr size_t kAeadTagSize = 16;

void chacha20Poly1305Seal(const uint8_t key[kAeadKeySize],
                          const uint8_t nonce[kAeadNonceSize],
                          const uint8_t* aad, size_t aad_length,
                          const uint8_t* plaintext, size_t length,
                          uint8_t* ciphertext, uint8_t tag[kAeadTagSize]);
bool chacha20Poly1305Open(const uint8_t key[kAeadKeySize],
                          const uint8_t nonce[kAeadNonceSize],
                          const uint8_t* aad, size_t aad_length,
                          const uint8_t* ciphertext, size_t length,
                          const uint8_t tag[kAeadTagSize], uint8_t* plaintext);

// Compares without an early exit, so the duration does not reveal the
// position of the first differing byte.
bool equalConstantTime(const uint8_t* a, const uint8_t* b, size_t length);

// Clears memory in a way the compiler cannot remove as a dead store.
void secureZero(void* data, size_t length);

// Writes lowercase hex and a terminating NUL. Returns false when out_size is
// smaller than 2 * length + 1.
bool hexEncode(const uint8_t* data, size_t length, char* out, size_t out_size);

// Decodes exactly out_length bytes. hex_length must be 2 * out_length and every
// character a hex digit (either case); nothing else is accepted.
bool hexDecode(const char* hex, size_t hex_length, uint8_t* out,
               size_t out_length);

}  // namespace ht_crypto
