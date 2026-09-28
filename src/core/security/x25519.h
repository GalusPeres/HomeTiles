#pragma once

#include <stddef.h>
#include <stdint.h>

// X25519 (RFC 7748) through mbedTLS for the number-comparison pairing with the
// HomeTiles Bridge. No curve arithmetic is written here; this only moves the
// raw little-endian keys in and out of mbedTLS.
namespace x25519 {

constexpr size_t kKeySize = 32;

// Public key of a raw 32-byte private key (clamped as in RFC 7748).
bool publicKey(const uint8_t private_key[kKeySize], uint8_t public_key[kKeySize]);

// Shared secret with a peer public key; false on any error or an all-zero
// result (a low-order peer key).
bool sharedSecret(const uint8_t private_key[kKeySize],
                  const uint8_t peer_public_key[kKeySize], uint8_t out[kKeySize]);

// Checks the RFC 7748 section 6.1 vector through the same path. Run before
// each pairing attempt.
bool selfTest();

}  // namespace x25519
