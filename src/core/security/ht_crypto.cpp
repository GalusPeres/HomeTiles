#include "src/core/security/ht_crypto.h"

#include <string.h>

namespace ht_crypto {

namespace {

constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t value, unsigned bits) {
  return (value >> bits) | (value << (32U - bits));
}

inline uint32_t loadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void storeBe32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value >> 24);
  p[1] = static_cast<uint8_t>(value >> 16);
  p[2] = static_cast<uint8_t>(value >> 8);
  p[3] = static_cast<uint8_t>(value);
}

void sha256Transform(uint32_t state[8], const uint8_t block[kSha256BlockSize]) {
  uint32_t w[64];
  for (size_t i = 0; i < 16; ++i) w[i] = loadBe32(block + i * 4);
  for (size_t i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (size_t i = 0; i < 64; ++i) {
    const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t choice = (e & f) ^ (~e & g);
    const uint32_t t1 = h + s1 + choice + kSha256K[i] + w[i];
    const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
  secureZero(w, sizeof(w));
}

inline uint32_t loadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline void storeLe32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
  p[2] = static_cast<uint8_t>(value >> 16);
  p[3] = static_cast<uint8_t>(value >> 24);
}

inline uint32_t rotl(uint32_t value, unsigned bits) {
  return (value << bits) | (value >> (32U - bits));
}

inline void quarterRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
  a += b; d ^= a; d = rotl(d, 16);
  c += d; b ^= c; b = rotl(b, 12);
  a += b; d ^= a; d = rotl(d, 8);
  c += d; b ^= c; b = rotl(b, 7);
}

// One 64-byte ChaCha20 key stream block (RFC 8439 section 2.3).
void chacha20Block(const uint8_t key[32], uint32_t counter,
                   const uint8_t nonce[12], uint8_t out[64]) {
  uint32_t state[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
  for (size_t i = 0; i < 8; ++i) state[4 + i] = loadLe32(key + i * 4);
  state[12] = counter;
  for (size_t i = 0; i < 3; ++i) state[13 + i] = loadLe32(nonce + i * 4);
  uint32_t x[16];
  memcpy(x, state, sizeof(x));
  for (int round = 0; round < 10; ++round) {
    quarterRound(x[0], x[4], x[8], x[12]);
    quarterRound(x[1], x[5], x[9], x[13]);
    quarterRound(x[2], x[6], x[10], x[14]);
    quarterRound(x[3], x[7], x[11], x[15]);
    quarterRound(x[0], x[5], x[10], x[15]);
    quarterRound(x[1], x[6], x[11], x[12]);
    quarterRound(x[2], x[7], x[8], x[13]);
    quarterRound(x[3], x[4], x[9], x[14]);
  }
  for (size_t i = 0; i < 16; ++i) storeLe32(out + i * 4, x[i] + state[i]);
  secureZero(x, sizeof(x));
  secureZero(state, sizeof(state));
}

void chacha20Xor(const uint8_t key[32], uint32_t counter,
                 const uint8_t nonce[12], const uint8_t* in, size_t length,
                 uint8_t* out) {
  uint8_t block[64];
  size_t offset = 0;
  while (offset < length) {
    chacha20Block(key, counter++, nonce, block);
    const size_t take = length - offset < 64 ? length - offset : 64;
    for (size_t i = 0; i < take; ++i) out[offset + i] = in[offset + i] ^ block[i];
    offset += take;
  }
  secureZero(block, sizeof(block));
}

// Poly1305 with 26-bit limbs (the well-known "donna" 32-bit layout).
struct Poly1305 {
  uint32_t r[5];
  uint32_t h[5];
  uint32_t pad[4];
  uint8_t buffer[16];
  size_t leftover;
};

void poly1305Init(Poly1305& st, const uint8_t key[32]) {
  st.r[0] = loadLe32(key + 0) & 0x3ffffff;
  st.r[1] = (loadLe32(key + 3) >> 2) & 0x3ffff03;
  st.r[2] = (loadLe32(key + 6) >> 4) & 0x3ffc0ff;
  st.r[3] = (loadLe32(key + 9) >> 6) & 0x3f03fff;
  st.r[4] = (loadLe32(key + 12) >> 8) & 0x00fffff;
  for (size_t i = 0; i < 5; ++i) st.h[i] = 0;
  for (size_t i = 0; i < 4; ++i) st.pad[i] = loadLe32(key + 16 + i * 4);
  st.leftover = 0;
}

void poly1305Blocks(Poly1305& st, const uint8_t* m, size_t bytes, bool final_block) {
  const uint32_t hibit = final_block ? 0 : (1UL << 24);
  const uint32_t r0 = st.r[0], r1 = st.r[1], r2 = st.r[2], r3 = st.r[3], r4 = st.r[4];
  const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
  uint32_t h0 = st.h[0], h1 = st.h[1], h2 = st.h[2], h3 = st.h[3], h4 = st.h[4];
  while (bytes >= 16) {
    h0 += loadLe32(m + 0) & 0x3ffffff;
    h1 += (loadLe32(m + 3) >> 2) & 0x3ffffff;
    h2 += (loadLe32(m + 6) >> 4) & 0x3ffffff;
    h3 += (loadLe32(m + 9) >> 6) & 0x3ffffff;
    h4 += (loadLe32(m + 12) >> 8) | hibit;

    uint64_t d0 = static_cast<uint64_t>(h0) * r0 + static_cast<uint64_t>(h1) * s4 +
                  static_cast<uint64_t>(h2) * s3 + static_cast<uint64_t>(h3) * s2 +
                  static_cast<uint64_t>(h4) * s1;
    uint64_t d1 = static_cast<uint64_t>(h0) * r1 + static_cast<uint64_t>(h1) * r0 +
                  static_cast<uint64_t>(h2) * s4 + static_cast<uint64_t>(h3) * s3 +
                  static_cast<uint64_t>(h4) * s2;
    uint64_t d2 = static_cast<uint64_t>(h0) * r2 + static_cast<uint64_t>(h1) * r1 +
                  static_cast<uint64_t>(h2) * r0 + static_cast<uint64_t>(h3) * s4 +
                  static_cast<uint64_t>(h4) * s3;
    uint64_t d3 = static_cast<uint64_t>(h0) * r3 + static_cast<uint64_t>(h1) * r2 +
                  static_cast<uint64_t>(h2) * r1 + static_cast<uint64_t>(h3) * r0 +
                  static_cast<uint64_t>(h4) * s4;
    uint64_t d4 = static_cast<uint64_t>(h0) * r4 + static_cast<uint64_t>(h1) * r3 +
                  static_cast<uint64_t>(h2) * r2 + static_cast<uint64_t>(h3) * r1 +
                  static_cast<uint64_t>(h4) * r0;

    uint32_t c = static_cast<uint32_t>(d0 >> 26);
    h0 = static_cast<uint32_t>(d0) & 0x3ffffff;
    d1 += c; c = static_cast<uint32_t>(d1 >> 26); h1 = static_cast<uint32_t>(d1) & 0x3ffffff;
    d2 += c; c = static_cast<uint32_t>(d2 >> 26); h2 = static_cast<uint32_t>(d2) & 0x3ffffff;
    d3 += c; c = static_cast<uint32_t>(d3 >> 26); h3 = static_cast<uint32_t>(d3) & 0x3ffffff;
    d4 += c; c = static_cast<uint32_t>(d4 >> 26); h4 = static_cast<uint32_t>(d4) & 0x3ffffff;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= 0x3ffffff;
    h1 += c;
    m += 16;
    bytes -= 16;
  }
  st.h[0] = h0; st.h[1] = h1; st.h[2] = h2; st.h[3] = h3; st.h[4] = h4;
}

void poly1305Update(Poly1305& st, const uint8_t* m, size_t bytes) {
  if (st.leftover) {
    size_t want = 16 - st.leftover;
    if (want > bytes) want = bytes;
    memcpy(st.buffer + st.leftover, m, want);
    bytes -= want;
    m += want;
    st.leftover += want;
    if (st.leftover < 16) return;
    poly1305Blocks(st, st.buffer, 16, false);
    st.leftover = 0;
  }
  if (bytes >= 16) {
    const size_t want = bytes & ~static_cast<size_t>(15);
    poly1305Blocks(st, m, want, false);
    m += want;
    bytes -= want;
  }
  if (bytes) {
    memcpy(st.buffer + st.leftover, m, bytes);
    st.leftover += bytes;
  }
}

void poly1305Finish(Poly1305& st, uint8_t mac[16]) {
  if (st.leftover) {
    size_t i = st.leftover;
    st.buffer[i++] = 1;
    for (; i < 16; ++i) st.buffer[i] = 0;
    poly1305Blocks(st, st.buffer, 16, true);
  }
  uint32_t h0 = st.h[0], h1 = st.h[1], h2 = st.h[2], h3 = st.h[3], h4 = st.h[4];
  uint32_t c = h1 >> 26;
  h1 &= 0x3ffffff;
  h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
  h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
  h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
  h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
  h1 += c;

  // Compute h - p and keep it when it does not underflow.
  uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
  uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
  uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
  uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
  uint32_t g4 = h4 + c - (1UL << 26);
  uint32_t mask = (g4 >> 31) - 1;
  g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
  mask = ~mask;
  h0 = (h0 & mask) | g0;
  h1 = (h1 & mask) | g1;
  h2 = (h2 & mask) | g2;
  h3 = (h3 & mask) | g3;
  h4 = (h4 & mask) | g4;

  h0 = h0 | (h1 << 26);
  h1 = (h1 >> 6) | (h2 << 20);
  h2 = (h2 >> 12) | (h3 << 14);
  h3 = (h3 >> 18) | (h4 << 8);

  uint64_t f = static_cast<uint64_t>(h0) + st.pad[0];
  h0 = static_cast<uint32_t>(f);
  f = static_cast<uint64_t>(h1) + st.pad[1] + (f >> 32);
  h1 = static_cast<uint32_t>(f);
  f = static_cast<uint64_t>(h2) + st.pad[2] + (f >> 32);
  h2 = static_cast<uint32_t>(f);
  f = static_cast<uint64_t>(h3) + st.pad[3] + (f >> 32);
  h3 = static_cast<uint32_t>(f);
  storeLe32(mac + 0, h0);
  storeLe32(mac + 4, h1);
  storeLe32(mac + 8, h2);
  storeLe32(mac + 12, h3);
  secureZero(&st, sizeof(st));
}

// Tag over aad || pad16 || ciphertext || pad16 || le64(aad) || le64(ct).
void aeadTag(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad,
             size_t aad_length, const uint8_t* ciphertext, size_t length,
             uint8_t tag[16]) {
  uint8_t block[64];
  chacha20Block(key, 0, nonce, block);
  Poly1305 mac;
  poly1305Init(mac, block);
  secureZero(block, sizeof(block));
  static const uint8_t kZeros[16] = {};
  if (aad_length) poly1305Update(mac, aad, aad_length);
  if (aad_length % 16) poly1305Update(mac, kZeros, 16 - aad_length % 16);
  if (length) poly1305Update(mac, ciphertext, length);
  if (length % 16) poly1305Update(mac, kZeros, 16 - length % 16);
  uint8_t lengths[16];
  for (size_t i = 0; i < 8; ++i) {
    lengths[i] = static_cast<uint8_t>(static_cast<uint64_t>(aad_length) >> (8 * i));
    lengths[8 + i] = static_cast<uint8_t>(static_cast<uint64_t>(length) >> (8 * i));
  }
  poly1305Update(mac, lengths, sizeof(lengths));
  poly1305Finish(mac, tag);
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

void sha256Init(Sha256& ctx) {
  static constexpr uint32_t kInitial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                           0xa54ff53a, 0x510e527f, 0x9b05688c,
                                           0x1f83d9ab, 0x5be0cd19};
  memcpy(ctx.state, kInitial, sizeof(kInitial));
  ctx.total_bytes = 0;
  ctx.buffer_length = 0;
}

void sha256Update(Sha256& ctx, const void* data, size_t length) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  if (!bytes || length == 0) return;
  ctx.total_bytes += length;
  if (ctx.buffer_length) {
    const size_t take = kSha256BlockSize - ctx.buffer_length < length
                            ? kSha256BlockSize - ctx.buffer_length
                            : length;
    memcpy(ctx.buffer + ctx.buffer_length, bytes, take);
    ctx.buffer_length += take;
    bytes += take;
    length -= take;
    if (ctx.buffer_length < kSha256BlockSize) return;
    sha256Transform(ctx.state, ctx.buffer);
    ctx.buffer_length = 0;
  }
  while (length >= kSha256BlockSize) {
    sha256Transform(ctx.state, bytes);
    bytes += kSha256BlockSize;
    length -= kSha256BlockSize;
  }
  if (length) {
    memcpy(ctx.buffer, bytes, length);
    ctx.buffer_length = length;
  }
}

void sha256Final(Sha256& ctx, uint8_t out[kSha256Size]) {
  const uint64_t total_bits = ctx.total_bytes * 8U;
  ctx.buffer[ctx.buffer_length++] = 0x80;
  if (ctx.buffer_length > kSha256BlockSize - 8) {
    memset(ctx.buffer + ctx.buffer_length, 0,
           kSha256BlockSize - ctx.buffer_length);
    sha256Transform(ctx.state, ctx.buffer);
    ctx.buffer_length = 0;
  }
  memset(ctx.buffer + ctx.buffer_length, 0,
         kSha256BlockSize - 8 - ctx.buffer_length);
  for (size_t i = 0; i < 8; ++i) {
    ctx.buffer[kSha256BlockSize - 1 - i] =
        static_cast<uint8_t>(total_bits >> (8 * i));
  }
  sha256Transform(ctx.state, ctx.buffer);
  for (size_t i = 0; i < 8; ++i) storeBe32(out + i * 4, ctx.state[i]);
  secureZero(&ctx, sizeof(ctx));
}

void sha256(const void* data, size_t length, uint8_t out[kSha256Size]) {
  Sha256 ctx;
  sha256Init(ctx);
  sha256Update(ctx, data, length);
  sha256Final(ctx, out);
}

void hmacSha256Init(HmacSha256& ctx, const uint8_t* key, size_t key_length) {
  uint8_t block[kSha256BlockSize] = {};
  if (key_length > kSha256BlockSize) {
    sha256(key, key_length, block);
  } else if (key && key_length) {
    memcpy(block, key, key_length);
  }
  uint8_t pad[kSha256BlockSize];
  for (size_t i = 0; i < kSha256BlockSize; ++i) pad[i] = block[i] ^ 0x36;
  sha256Init(ctx.inner);
  sha256Update(ctx.inner, pad, sizeof(pad));
  for (size_t i = 0; i < kSha256BlockSize; ++i) pad[i] = block[i] ^ 0x5c;
  sha256Init(ctx.outer);
  sha256Update(ctx.outer, pad, sizeof(pad));
  secureZero(block, sizeof(block));
  secureZero(pad, sizeof(pad));
}

void hmacSha256Update(HmacSha256& ctx, const void* data, size_t length) {
  sha256Update(ctx.inner, data, length);
}

void hmacSha256Final(HmacSha256& ctx, uint8_t out[kSha256Size]) {
  uint8_t inner_digest[kSha256Size];
  sha256Final(ctx.inner, inner_digest);
  sha256Update(ctx.outer, inner_digest, sizeof(inner_digest));
  sha256Final(ctx.outer, out);
  secureZero(inner_digest, sizeof(inner_digest));
}

void hmacSha256(const uint8_t* key, size_t key_length, const void* data,
                size_t length, uint8_t out[kSha256Size]) {
  HmacSha256 ctx;
  hmacSha256Init(ctx, key, key_length);
  hmacSha256Update(ctx, data, length);
  hmacSha256Final(ctx, out);
}

void hkdfSha256(const uint8_t* salt, size_t salt_length, const uint8_t* ikm,
                size_t ikm_length, const uint8_t* info, size_t info_length,
                uint8_t* out, size_t out_length) {
  if (!out || out_length == 0 || out_length > 255 * kSha256Size) return;
  uint8_t prk[kSha256Size];
  hmacSha256(salt, salt_length, ikm, ikm_length, prk);
  uint8_t block[kSha256Size];
  size_t produced = 0;
  for (uint8_t counter = 1; produced < out_length; ++counter) {
    HmacSha256 mac;
    hmacSha256Init(mac, prk, sizeof(prk));
    if (counter > 1) hmacSha256Update(mac, block, sizeof(block));
    if (info && info_length) hmacSha256Update(mac, info, info_length);
    hmacSha256Update(mac, &counter, 1);
    hmacSha256Final(mac, block);
    const size_t take = out_length - produced < kSha256Size
                            ? out_length - produced
                            : kSha256Size;
    memcpy(out + produced, block, take);
    produced += take;
  }
  secureZero(prk, sizeof(prk));
  secureZero(block, sizeof(block));
}

void chacha20Poly1305Seal(const uint8_t key[kAeadKeySize],
                          const uint8_t nonce[kAeadNonceSize],
                          const uint8_t* aad, size_t aad_length,
                          const uint8_t* plaintext, size_t length,
                          uint8_t* ciphertext, uint8_t tag[kAeadTagSize]) {
  chacha20Xor(key, 1, nonce, plaintext, length, ciphertext);
  aeadTag(key, nonce, aad, aad_length, ciphertext, length, tag);
}

bool chacha20Poly1305Open(const uint8_t key[kAeadKeySize],
                          const uint8_t nonce[kAeadNonceSize],
                          const uint8_t* aad, size_t aad_length,
                          const uint8_t* ciphertext, size_t length,
                          const uint8_t tag[kAeadTagSize], uint8_t* plaintext) {
  uint8_t expected[kAeadTagSize];
  aeadTag(key, nonce, aad, aad_length, ciphertext, length, expected);
  const bool valid = equalConstantTime(expected, tag, kAeadTagSize);
  secureZero(expected, sizeof(expected));
  if (!valid) {
    if (plaintext && plaintext != ciphertext) secureZero(plaintext, length);
    return false;
  }
  chacha20Xor(key, 1, nonce, ciphertext, length, plaintext);
  return true;
}

bool equalConstantTime(const uint8_t* a, const uint8_t* b, size_t length) {
  if (!a || !b) return false;
  uint8_t difference = 0;
  for (size_t i = 0; i < length; ++i) difference |= a[i] ^ b[i];
  return difference == 0;
}

void secureZero(void* data, size_t length) {
  if (!data) return;
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
  while (length--) *bytes++ = 0;
}

bool hexEncode(const uint8_t* data, size_t length, char* out, size_t out_size) {
  static constexpr char kDigits[] = "0123456789abcdef";
  if (!out || out_size < length * 2 + 1 || (!data && length)) return false;
  for (size_t i = 0; i < length; ++i) {
    out[i * 2] = kDigits[data[i] >> 4];
    out[i * 2 + 1] = kDigits[data[i] & 0x0f];
  }
  out[length * 2] = '\0';
  return true;
}

bool hexDecode(const char* hex, size_t hex_length, uint8_t* out,
               size_t out_length) {
  if (!hex || !out || hex_length != out_length * 2) return false;
  for (size_t i = 0; i < out_length; ++i) {
    const int high = hexValue(hex[i * 2]);
    const int low = hexValue(hex[i * 2 + 1]);
    if (high < 0 || low < 0) {
      secureZero(out, out_length);
      return false;
    }
    out[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

}  // namespace ht_crypto
