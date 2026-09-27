// The firmware's portable SHA-256/HMAC/HKDF/ChaCha20-Poly1305
// (src/core/security/ht_crypto.cpp) must match OpenSSL bit for bit: the
// browser, the Bridge (Python hashlib/cryptography) and the panel derive the
// same Web Admin login proofs and command channel keys and envelopes.
import assert from 'node:assert/strict';
import {createCipheriv, createHash, createHmac, hkdfSync} from 'node:crypto';

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
  // HKDF-SHA256 for several salt/info/output lengths.
  for (uint32_t out_length : {1u, 16u, 32u, 33u, 64u, 100u}) {
    auto salt = xorshift(out_length + 900, out_length % 40);
    auto ikm = xorshift(out_length + 901, 25);
    auto info = xorshift(out_length + 902, out_length % 13);
    std::vector<uint8_t> okm(out_length);
    ht_crypto::hkdfSha256(salt.data(), salt.size(), ikm.data(), ikm.size(),
                          info.data(), info.size(), okm.data(), okm.size());
    char text[2 * 100 + 1];
    ht_crypto::hexEncode(okm.data(), okm.size(), text, sizeof(text));
    std::printf("hkdf %s\n", text);
  }
  // ChaCha20-Poly1305 for lengths around the 16/64-byte block edges.
  for (uint32_t length : {0u, 1u, 15u, 16u, 17u, 63u, 64u, 65u, 127u, 128u, 300u, 2049u}) {
    auto key = xorshift(length + 500, 32);
    auto nonce = xorshift(length + 501, 12);
    auto aad = xorshift(length + 502, length % 29);
    auto plaintext = xorshift(length + 503, length);
    std::vector<uint8_t> ciphertext(length);
    uint8_t tag[16];
    ht_crypto::chacha20Poly1305Seal(key.data(), nonce.data(), aad.data(), aad.size(),
                                    plaintext.data(), length, ciphertext.data(), tag);
    std::vector<uint8_t> opened(length);
    if (!ht_crypto::chacha20Poly1305Open(key.data(), nonce.data(), aad.data(), aad.size(),
                                         ciphertext.data(), length, tag, opened.data())) return 10;
    if (length && std::memcmp(opened.data(), plaintext.data(), length) != 0) return 11;
    // Any flipped bit in tag, ciphertext or AAD is rejected.
    tag[length % 16] ^= 0x20;
    if (ht_crypto::chacha20Poly1305Open(key.data(), nonce.data(), aad.data(), aad.size(),
                                        ciphertext.data(), length, tag, opened.data())) return 12;
    tag[length % 16] ^= 0x20;
    if (length) {
      ciphertext[length / 2] ^= 1;
      if (ht_crypto::chacha20Poly1305Open(key.data(), nonce.data(), aad.data(), aad.size(),
                                          ciphertext.data(), length, tag, opened.data())) return 13;
      ciphertext[length / 2] ^= 1;
    }
    if (!aad.empty()) {
      aad[0] ^= 1;
      if (ht_crypto::chacha20Poly1305Open(key.data(), nonce.data(), aad.data(), aad.size(),
                                          ciphertext.data(), length, tag, opened.data())) return 14;
      aad[0] ^= 1;
    }
    std::vector<char> text(2 * (length + 16) + 1);
    std::vector<uint8_t> sealed(ciphertext);
    sealed.insert(sealed.end(), tag, tag + 16);
    ht_crypto::hexEncode(sealed.data(), sealed.size(), text.data(), text.size());
    std::printf("aead %s\n", text.data());
  }
  std::vector<uint8_t> million(1000000, 'a');
  ht_crypto::sha256(million.data(), million.size(), digest);
  print_hex("million", digest, 32);

  // RFC 8439 section 2.8.2 AEAD vector.
  {
    const char* text = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    uint8_t key[32], nonce[12] = {0x07, 0, 0, 0, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(0x80 + i);
    const uint8_t aad[12] = {0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7};
    const uint8_t expected_tag[16] = {0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09, 0xe2, 0x6a, 0x7e, 0x90, 0x2e, 0xcb, 0xd0, 0x60, 0x06, 0x91};
    const uint8_t expected_start[8] = {0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb};
    const size_t length = std::strlen(text);
    std::vector<uint8_t> out(length);
    uint8_t tag[16];
    ht_crypto::chacha20Poly1305Seal(key, nonce, aad, sizeof(aad),
                                    reinterpret_cast<const uint8_t*>(text), length, out.data(), tag);
    if (std::memcmp(tag, expected_tag, 16) != 0 || std::memcmp(out.data(), expected_start, 8) != 0) return 15;
  }
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
  for (const outLength of [1, 16, 32, 33, 64, 100]) {
    const salt = xorshiftBytes(outLength + 900, outLength % 40);
    const ikm = xorshiftBytes(outLength + 901, 25);
    const info = xorshiftBytes(outLength + 902, outLength % 13);
    const expected = Buffer.from(hkdfSync('sha256', ikm, salt, info, outLength)).toString('hex');
    assert.equal(lines[index++], `hkdf ${expected}`, `HKDF ${outLength} bytes`);
  }
  for (const length of [0, 1, 15, 16, 17, 63, 64, 65, 127, 128, 300, 2049]) {
    const key = xorshiftBytes(length + 500, 32);
    const nonce = xorshiftBytes(length + 501, 12);
    const aad = xorshiftBytes(length + 502, length % 29);
    const plaintext = xorshiftBytes(length + 503, length);
    const cipher = createCipheriv('chacha20-poly1305', key, nonce, {authTagLength: 16});
    cipher.setAAD(aad, {plaintextLength: length});
    const ciphertext = Buffer.concat([cipher.update(plaintext), cipher.final()]);
    const expected = Buffer.concat([ciphertext, cipher.getAuthTag()]).toString('hex');
    assert.equal(lines[index++], `aead ${expected}`, `ChaCha20-Poly1305 ${length} bytes`);
  }
  assert.equal(lines[index++],
    'million cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0',
    'FIPS 180-2 one million "a" vector');
  assert.equal(index, lines.length);
  console.log(`Portable SHA-256/HMAC/HKDF/ChaCha20-Poly1305 matched OpenSSL for ${lines.length} vectors`);
}
