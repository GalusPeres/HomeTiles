// The firmware's portable SHA-256/HMAC (src/core/security/ht_crypto.cpp) must
// match OpenSSL bit for bit: the browser, the Bridge (Python hashlib) and the
// panel all derive the same Web Admin login key and proofs.
import assert from 'node:assert/strict';
import {createHash, createHmac} from 'node:crypto';

import {compileAndRun} from '../../lib/cpp-host.mjs';

// Deterministic inputs shared by the harness and this script.
function xorshiftBytes(seed, length) {
  let state = seed >>> 0 || 1;
  const out = Buffer.alloc(length);
  for (let i = 0; i < length; i++) {
    state ^= state << 13; state >>>= 0;
    state ^= state >>> 17;
    state ^= state << 5; state >>>= 0;
    out[i] = state & 0xff;
  }
  return out;
}

const harness = String.raw`
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "src/core/security/ht_crypto.h"

static std::vector<uint8_t> xorshift(uint32_t seed, size_t length) {
  uint32_t state = seed ? seed : 1;
  std::vector<uint8_t> out(length);
  for (size_t i = 0; i < length; ++i) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    out[i] = static_cast<uint8_t>(state & 0xff);
  }
  return out;
}

static void print_hex(const char* tag, const uint8_t* data, size_t length) {
  char text[2 * 64 + 1];
  if (!ht_crypto::hexEncode(data, length, text, sizeof(text))) std::abort();
  std::printf("%s %s\n", tag, text);
}

int main() {
  uint8_t digest[32];
  for (uint32_t length = 0; length <= 300; ++length) {
    auto data = xorshift(length + 11, length);
    ht_crypto::sha256(data.data(), data.size(), digest);
    print_hex("sha", digest, 32);
    // Streaming in uneven pieces must give the same digest.
    ht_crypto::Sha256 ctx;
    ht_crypto::sha256Init(ctx);
    size_t offset = 0, step = 1;
    while (offset < data.size()) {
      size_t take = step < data.size() - offset ? step : data.size() - offset;
      ht_crypto::sha256Update(ctx, data.data() + offset, take);
      offset += take;
      step = step * 3 % 97 + 1;
    }
    uint8_t streamed[32];
    ht_crypto::sha256Final(ctx, streamed);
    if (std::memcmp(streamed, digest, 32) != 0) return 2;
  }
  for (uint32_t key_length : {0u, 1u, 16u, 32u, 63u, 64u, 65u, 131u}) {
    auto key = xorshift(key_length + 101, key_length);
    for (uint32_t length : {0u, 1u, 32u, 55u, 56u, 64u, 200u}) {
      auto data = xorshift(length * 7 + key_length + 3, length);
      ht_crypto::hmacSha256(key.data(), key.size(), data.data(), data.size(), digest);
      print_hex("hmac", digest, 32);
    }
  }
  std::vector<uint8_t> million(1000000, 'a');
  ht_crypto::sha256(million.data(), million.size(), digest);
  print_hex("million", digest, 32);

  // Hex helpers: exact length, both cases accepted, anything else rejected.
  uint8_t bytes[4];
  if (!ht_crypto::hexDecode("0aFf10c3", 8, bytes, 4)) return 3;
  if (bytes[0] != 0x0a || bytes[1] != 0xff || bytes[2] != 0x10 || bytes[3] != 0xc3) return 4;
  if (ht_crypto::hexDecode("0aFf10c", 7, bytes, 4)) return 5;
  if (ht_crypto::hexDecode("0aFf10cg", 8, bytes, 4)) return 6;
  if (ht_crypto::hexDecode("0aFf10c3aa", 10, bytes, 4)) return 7;
  char small[8];
  if (ht_crypto::hexEncode(bytes, 4, small, sizeof(small))) return 8;
  const uint8_t a[3] = {1, 2, 3}, b[3] = {1, 2, 4};
  if (!ht_crypto::equalConstantTime(a, a, 3) || ht_crypto::equalConstantTime(a, b, 3)) return 9;
  return 0;
}
`;

const stdout = compileAndRun({
  label: 'Portable SHA-256/HMAC harness',
  harness,
  sources: ['src/core/security/ht_crypto.cpp']
});
if (stdout !== null) {
  const lines = stdout.trim().split('\n');
  let index = 0;
  for (let length = 0; length <= 300; length++) {
    const expected = createHash('sha256').update(xorshiftBytes(length + 11, length)).digest('hex');
    assert.equal(lines[index++], `sha ${expected}`, `SHA-256 of ${length} bytes`);
  }
  for (const keyLength of [0, 1, 16, 32, 63, 64, 65, 131]) {
    const key = xorshiftBytes(keyLength + 101, keyLength);
    for (const length of [0, 1, 32, 55, 56, 64, 200]) {
      const data = xorshiftBytes(length * 7 + keyLength + 3, length);
      const expected = createHmac('sha256', key).update(data).digest('hex');
      assert.equal(lines[index++], `hmac ${expected}`, `HMAC key ${keyLength} data ${length}`);
    }
  }
  assert.equal(lines[index++],
    'million cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0',
    'FIPS 180-2 one million "a" vector');
  assert.equal(index, lines.length);
  console.log(`Portable SHA-256/HMAC matched OpenSSL for ${lines.length} vectors`);
}
