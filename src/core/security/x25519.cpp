#include "src/core/security/x25519.h"

#include <string.h>

#include <mbedtls/ecp.h>

#include "src/core/security/ht_crypto.h"
#include "src/core/security/secure_random.h"

#if !defined(MBEDTLS_ECP_DP_CURVE25519_ENABLED)
#error "The HomeTiles Bridge pairing needs Curve25519 in mbedTLS"
#endif

namespace x25519 {

namespace {

// Random source for mbedTLS's coordinate blinding.
int randomBytes(void*, unsigned char* out, size_t length) {
  secure_random::fill(out, length);
  return 0;
}

// scalar * point, or scalar * base point when point is nullptr.
bool multiply(const uint8_t scalar[kKeySize], const uint8_t* point,
              uint8_t out[kKeySize]) {
  uint8_t clamped[kKeySize];
  memcpy(clamped, scalar, kKeySize);
  clamped[0] &= 248;
  clamped[31] &= 127;
  clamped[31] |= 64;

  mbedtls_ecp_group group;
  mbedtls_mpi d;
  mbedtls_ecp_point input;
  mbedtls_ecp_point result;
  mbedtls_ecp_group_init(&group);
  mbedtls_mpi_init(&d);
  mbedtls_ecp_point_init(&input);
  mbedtls_ecp_point_init(&result);

  bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
            mbedtls_mpi_read_binary_le(&d, clamped, kKeySize) == 0;
  if (ok && point) {
    // Reads the little-endian u-coordinate and masks its top bit (RFC 7748).
    ok = mbedtls_ecp_point_read_binary(&group, &input, point, kKeySize) == 0;
  }
  if (ok) {
    ok = mbedtls_ecp_mul(&group, &result, &d, point ? &input : &group.G,
                         randomBytes, nullptr) == 0;
  }
  size_t written = 0;
  if (ok) {
    ok = mbedtls_ecp_point_write_binary(&group, &result, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                        &written, out, kKeySize) == 0 &&
         written == kKeySize;
  }

  mbedtls_ecp_point_free(&result);
  mbedtls_ecp_point_free(&input);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&group);
  ht_crypto::secureZero(clamped, sizeof(clamped));
  if (!ok) ht_crypto::secureZero(out, kKeySize);
  return ok;
}

}  // namespace

bool publicKey(const uint8_t private_key[kKeySize], uint8_t public_key[kKeySize]) {
  if (!private_key || !public_key) return false;
  return multiply(private_key, nullptr, public_key);
}

bool sharedSecret(const uint8_t private_key[kKeySize],
                  const uint8_t peer_public_key[kKeySize], uint8_t out[kKeySize]) {
  if (!private_key || !peer_public_key || !out) return false;
  if (!multiply(private_key, peer_public_key, out)) return false;
  uint8_t any = 0;
  for (size_t i = 0; i < kKeySize; ++i) any |= out[i];
  if (any == 0) {
    ht_crypto::secureZero(out, kKeySize);
    return false;
  }
  return true;
}

bool selfTest() {
  static const uint8_t kAlicePrivate[kKeySize] = {
      0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1,
      0x72, 0x51, 0xb2, 0x66, 0x45, 0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0,
      0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a};
  static const uint8_t kBobPublic[kKeySize] = {
      0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4, 0xd3, 0x5b, 0x61,
      0xc2, 0xec, 0xe4, 0x35, 0x37, 0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78,
      0x67, 0x4d, 0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f};
  static const uint8_t kShared[kKeySize] = {
      0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1, 0x72, 0x8e, 0x3b,
      0xf4, 0x80, 0x35, 0x0f, 0x25, 0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1,
      0x9e, 0x33, 0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42};
  uint8_t shared[kKeySize];
  const bool ok = sharedSecret(kAlicePrivate, kBobPublic, shared) &&
                  ht_crypto::equalConstantTime(shared, kShared, kKeySize);
  ht_crypto::secureZero(shared, sizeof(shared));
  return ok;
}

}  // namespace x25519
