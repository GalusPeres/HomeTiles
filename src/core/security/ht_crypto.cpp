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
