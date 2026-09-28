#pragma once

// Platform-independent part of the optional Web Admin password.
//
// Protocol (see also docs-dev/command-encryption.md for the Bridge side):
//   GET  /api/auth/challenge -> {"enabled":true,"salt":<hex 16 B>,"nonce":<hex 32 B>,
//                                "iter":<PBKDF2 iterations>}
//   client: key   = PBKDF2-HMAC-SHA256(UTF-8 password, salt_bytes, iter, 32 B)
//           proof = HMAC-SHA256(key, nonce_bytes)
//   POST /api/auth/login {"nonce":<hex>,"proof":<hex>}
//        -> {"csrf":<hex>,"server_proof":<hex>} and an HttpOnly, SameSite=Lax
//           session cookie that lasts 30 days. server_proof = HMAC-SHA256(key, label || nonce ||
//           proof) lets a client that knows the password recognise the real
//           panel before it sends anything sensitive (the Bridge pairing push).
// The device stores only salt, iteration count and key; it never derives a key
// itself, so the slow PBKDF2 runs only in the browser or the Bridge. A nonce is
// random, single use and valid for 60 s. Consecutive failures lock the login
// with a growing delay.
//
// Everything here takes the current time and a random source as parameters,
// so the host tests in tools/tests/web/ exercise this exact code.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "src/core/security/ht_crypto.h"

namespace web_admin_auth {

constexpr size_t kSaltSize = 16;
constexpr size_t kKeySize = 32;
constexpr size_t kNonceSize = 32;
constexpr size_t kProofSize = 32;
// Session id and CSRF token: 128 random bits each, 32 hex characters.
constexpr size_t kTokenSize = 16;
constexpr size_t kTokenHexSize = kTokenSize * 2 + 1;
constexpr size_t kMaxNonces = 4;
constexpr size_t kMaxSessions = 4;
constexpr uint32_t kNonceLifetimeMs = 60UL * 1000UL;
// A session lasts 30 days, also across restarts (see SessionRecord).
constexpr uint32_t kSessionMaxSeconds = 30UL * 24UL * 60UL * 60UL;
constexpr uint32_t kSessionMaxMs = kSessionMaxSeconds * 1000UL;
// Wall-clock seconds below this mean the clock is not set yet.
constexpr uint32_t kMinValidEpoch = 1700000000UL;
// Failed attempts before the first lockout; the delay then doubles from 1 s
// up to five minutes.
constexpr uint8_t kFreeFailures = 3;
constexpr uint32_t kFirstLockoutMs = 1000UL;
constexpr uint32_t kMaxLockoutMs = 5UL * 60UL * 1000UL;
constexpr char kSessionCookieName[] = "ht_session";
constexpr char kServerProofLabel[] = "HomeTiles-Web-Admin-server-v1";
// PBKDF2 iteration counts a client may choose when it sets the password. The
// Bridge refuses challenges outside this range, so a fake panel cannot stall it.
constexpr uint32_t kMinIterations = 10000;
constexpr uint32_t kMaxIterations = 1000000;

using RandomFill = void (*)(uint8_t* out, size_t length);

inline bool validIterations(uint32_t iterations) {
  return iterations >= kMinIterations && iterations <= kMaxIterations;
}

struct Credential {
  bool enabled = false;
  uint8_t salt[kSaltSize] = {};
  uint32_t iterations = 0;
  uint8_t key[kKeySize] = {};
};

struct NonceSlot {
  uint8_t value[kNonceSize];
  uint32_t issued_ms;
  bool used;
};

// The panel keeps only the SHA-256 of a session id, so neither RAM nor the
// stored record holds a usable cookie.
struct SessionSlot {
  uint8_t id_hash[ht_crypto::kSha256Size];
  uint8_t csrf[kTokenSize];
  uint32_t created_ms;
  uint32_t last_seen_ms;
  uint32_t expires_epoch;  // 0 while the clock was not set at login
  bool used;
};

// Short-lived login state. The firmware allocates it only while a password is
// set, preferably in PSRAM.
struct Tables {
  NonceSlot nonces[kMaxNonces];
  SessionSlot sessions[kMaxSessions];
  uint8_t failures;
  bool locked;
  uint32_t locked_until_ms;
};

enum class LoginResult : uint8_t {
  Success,
  Disabled,
  UnknownNonce,
  InvalidProof,
  Locked,
};

enum class AccessResult : uint8_t {
  Allowed,
  Unauthenticated,
  CsrfMismatch,
};

inline bool elapsedAtLeast(uint32_t now, uint32_t since, uint32_t duration) {
  return static_cast<uint32_t>(now - since) >= duration;
}

inline void clearTables(Tables& tables) {
  ht_crypto::secureZero(&tables, sizeof(tables));
}

inline uint32_t lockoutMsForFailures(uint8_t failures) {
  if (failures < kFreeFailures) return 0;
  const uint8_t doublings = static_cast<uint8_t>(failures - kFreeFailures);
  if (doublings >= 20) return kMaxLockoutMs;
  const uint32_t delay = kFirstLockoutMs << doublings;
  return delay > kMaxLockoutMs ? kMaxLockoutMs : delay;
}

// Remaining lockout in milliseconds, 0 when a login attempt may be checked.
inline uint32_t lockoutRemainingMs(Tables& tables, uint32_t now) {
  if (!tables.locked) return 0;
  const int32_t remaining =
      static_cast<int32_t>(tables.locked_until_ms - now);
  if (remaining <= 0) {
    tables.locked = false;
    return 0;
  }
  return static_cast<uint32_t>(remaining);
}

inline void issueNonce(Tables& tables, uint32_t now, RandomFill random,
                       uint8_t out[kNonceSize]) {
  // Reuse a free or expired slot; with all four pending, the oldest one goes.
  size_t slot = 0;
  uint32_t oldest_age = 0;
  for (size_t i = 0; i < kMaxNonces; ++i) {
    NonceSlot& candidate = tables.nonces[i];
    if (!candidate.used ||
        elapsedAtLeast(now, candidate.issued_ms, kNonceLifetimeMs)) {
      slot = i;
      break;
    }
    const uint32_t age = static_cast<uint32_t>(now - candidate.issued_ms);
    if (age >= oldest_age) {
      oldest_age = age;
      slot = i;
    }
  }
  NonceSlot& target = tables.nonces[slot];
  random(target.value, kNonceSize);
  target.issued_ms = now;
  target.used = true;
  memcpy(out, target.value, kNonceSize);
}

// Removes the nonce whatever the outcome, so every nonce is single use.
inline bool consumeNonce(Tables& tables, uint32_t now,
                         const uint8_t nonce[kNonceSize]) {
  bool valid = false;
  for (NonceSlot& slot : tables.nonces) {
    if (!slot.used) continue;
    if (!ht_crypto::equalConstantTime(slot.value, nonce, kNonceSize)) continue;
    valid = !elapsedAtLeast(now, slot.issued_ms, kNonceLifetimeMs);
    ht_crypto::secureZero(&slot, sizeof(slot));
  }
  return valid;
}

inline void computeProof(const Credential& credential,
                         const uint8_t nonce[kNonceSize],
                         uint8_t out[kProofSize]) {
  ht_crypto::hmacSha256(credential.key, kKeySize, nonce, kNonceSize, out);
}

inline void computeServerProof(const Credential& credential,
                               const uint8_t nonce[kNonceSize],
                               const uint8_t proof[kProofSize],
                               uint8_t out[kProofSize]) {
  ht_crypto::HmacSha256 mac;
  ht_crypto::hmacSha256Init(mac, credential.key, kKeySize);
  ht_crypto::hmacSha256Update(mac, kServerProofLabel,
                              sizeof(kServerProofLabel) - 1);
  ht_crypto::hmacSha256Update(mac, nonce, kNonceSize);
  ht_crypto::hmacSha256Update(mac, proof, kProofSize);
  ht_crypto::hmacSha256Final(mac, out);
}

inline SessionSlot& allocateSession(Tables& tables, uint32_t now) {
  size_t slot = 0;
  uint32_t oldest_idle = 0;
  for (size_t i = 0; i < kMaxSessions; ++i) {
    SessionSlot& candidate = tables.sessions[i];
    if (!candidate.used) {
      slot = i;
      break;
    }
    const uint32_t idle = static_cast<uint32_t>(now - candidate.last_seen_ms);
    if (idle >= oldest_idle) {
      oldest_idle = idle;
      slot = i;
    }
  }
  return tables.sessions[slot];
}

// retry_after_ms receives the remaining lockout for Locked results.
inline LoginResult login(const Credential& credential, Tables& tables,
                         uint32_t now, RandomFill random,
                         const uint8_t nonce[kNonceSize],
                         const uint8_t proof[kProofSize],
                         uint8_t session_id_out[kTokenSize],
                         uint8_t csrf_out[kTokenSize],
                         uint8_t server_proof_out[kProofSize],
                         uint32_t* retry_after_ms) {
  if (retry_after_ms) *retry_after_ms = 0;
  if (!credential.enabled) return LoginResult::Disabled;
  const bool nonce_valid = consumeNonce(tables, now, nonce);
  const uint32_t remaining = lockoutRemainingMs(tables, now);
  if (remaining) {
    if (retry_after_ms) *retry_after_ms = remaining;
    return LoginResult::Locked;
  }
  // A stale nonce is not a password guess; the client simply retries.
  if (!nonce_valid) return LoginResult::UnknownNonce;

  uint8_t expected[kProofSize];
  computeProof(credential, nonce, expected);
  const bool matches = ht_crypto::equalConstantTime(expected, proof, kProofSize);
  ht_crypto::secureZero(expected, sizeof(expected));
  if (!matches) {
    if (tables.failures < 0xff) ++tables.failures;
    const uint32_t lockout = lockoutMsForFailures(tables.failures);
    if (lockout) {
      tables.locked = true;
      tables.locked_until_ms = now + lockout;
      if (retry_after_ms) *retry_after_ms = lockout;
    }
    return LoginResult::InvalidProof;
  }

  tables.failures = 0;
  tables.locked = false;
  SessionSlot& session = allocateSession(tables, now);
  random(session_id_out, kTokenSize);
  ht_crypto::sha256(session_id_out, kTokenSize, session.id_hash);
  random(session.csrf, kTokenSize);
  session.created_ms = now;
  session.last_seen_ms = now;
  session.expires_epoch = 0;
  session.used = true;
  memcpy(csrf_out, session.csrf, kTokenSize);
  computeServerProof(credential, nonce, proof, server_proof_out);
  return LoginResult::Success;
}

inline SessionSlot* findSession(Tables& tables, uint32_t now,
                                const uint8_t session_id[kTokenSize]) {
  uint8_t hash[ht_crypto::kSha256Size];
  ht_crypto::sha256(session_id, kTokenSize, hash);
  SessionSlot* match = nullptr;
  for (SessionSlot& slot : tables.sessions) {
    if (!slot.used) continue;
    if (elapsedAtLeast(now, slot.created_ms, kSessionMaxMs)) {
      ht_crypto::secureZero(&slot, sizeof(slot));
      continue;
    }
    if (ht_crypto::equalConstantTime(slot.id_hash, hash, sizeof(hash))) {
      match = &slot;
    }
  }
  ht_crypto::secureZero(hash, sizeof(hash));
  return match;
}

// Ends sessions whose 30 days have passed by the wall clock. That also bounds
// a session restored after a restart, whose uptime count starts again.
// Returns true when one ended.
inline bool expireSessions(Tables& tables, uint32_t epoch_now) {
  if (epoch_now < kMinValidEpoch) return false;
  bool ended = false;
  for (SessionSlot& slot : tables.sessions) {
    if (slot.used && slot.expires_epoch && epoch_now >= slot.expires_epoch) {
      ht_crypto::secureZero(&slot, sizeof(slot));
      ended = true;
    }
  }
  return ended;
}

// A request that changes anything (every method except GET and HEAD) must
// repeat the session's CSRF token in the X-HomeTiles-CSRF header.
inline AccessResult checkAccess(const Credential& credential, Tables& tables,
                                uint32_t now, const uint8_t* session_id,
                                const uint8_t* csrf, bool mutating) {
  if (!credential.enabled) return AccessResult::Allowed;
  if (!session_id) return AccessResult::Unauthenticated;
  SessionSlot* session = findSession(tables, now, session_id);
  if (!session) return AccessResult::Unauthenticated;
  if (mutating &&
      (!csrf || !ht_crypto::equalConstantTime(session->csrf, csrf, kTokenSize))) {
    return AccessResult::CsrfMismatch;
  }
  session->last_seen_ms = now;
  return AccessResult::Allowed;
}

inline bool endSession(Tables& tables, const uint8_t session_id[kTokenSize]) {
  uint8_t hash[ht_crypto::kSha256Size];
  ht_crypto::sha256(session_id, kTokenSize, hash);
  bool ended = false;
  for (SessionSlot& slot : tables.sessions) {
    if (slot.used && ht_crypto::equalConstantTime(slot.id_hash, hash, sizeof(hash))) {
      ht_crypto::secureZero(&slot, sizeof(slot));
      ended = true;
    }
  }
  ht_crypto::secureZero(hash, sizeof(hash));
  return ended;
}

// Reads the 32 hex characters of ht_session from a Cookie header such as
// "a=b; ht_session=0123...; c=d". Any other length or character rejects it.
inline bool parseSessionCookie(const char* header, uint8_t out[kTokenSize]) {
  if (!header) return false;
  const size_t name_length = sizeof(kSessionCookieName) - 1;
  const char* p = header;
  while (*p) {
    while (*p == ' ' || *p == ';' || *p == '\t') ++p;
    const char* name = p;
    while (*p && *p != '=' && *p != ';') ++p;
    const size_t length = static_cast<size_t>(p - name);
    if (*p != '=') continue;
    ++p;
    const char* value = p;
    while (*p && *p != ';') ++p;
    size_t value_length = static_cast<size_t>(p - value);
    while (value_length && (value[value_length - 1] == ' ' ||
                            value[value_length - 1] == '\t')) {
      --value_length;
    }
    if (length == name_length &&
        memcmp(name, kSessionCookieName, name_length) == 0) {
      return ht_crypto::hexDecode(value, value_length, out, kTokenSize);
    }
  }
  return false;
}

// Minimal reader for the flat JSON objects of the auth endpoints. It accepts
// only an ASCII string value without escapes, which covers every hex field.
inline bool jsonStringField(const char* json, const char* key, char* out,
                            size_t out_size) {
  if (!json || !key || !out || out_size == 0) return false;
  out[0] = '\0';
  const size_t key_length = strlen(key);
  for (const char* p = json; *p; ++p) {
    if (*p != '"' || strncmp(p + 1, key, key_length) != 0 ||
        p[1 + key_length] != '"') {
      continue;
    }
    const char* q = p + key_length + 2;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
    if (*q != ':') continue;
    ++q;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
    if (*q != '"') return false;
    ++q;
    size_t length = 0;
    while (q[length] && q[length] != '"') {
      if (q[length] == '\\' || static_cast<unsigned char>(q[length]) < 0x20) {
        return false;
      }
      ++length;
    }
    if (q[length] != '"' || length + 1 > out_size) return false;
    memcpy(out, q, length);
    out[length] = '\0';
    return true;
  }
  return false;
}

inline bool jsonTrueField(const char* json, const char* key) {
  if (!json || !key) return false;
  const size_t key_length = strlen(key);
  for (const char* p = json; *p; ++p) {
    if (*p != '"' || strncmp(p + 1, key, key_length) != 0 ||
        p[1 + key_length] != '"') {
      continue;
    }
    const char* q = p + key_length + 2;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
    if (*q != ':') continue;
    ++q;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
    return strncmp(q, "true", 4) == 0;
  }
  return false;
}

// Reads an unsigned JSON number of at most 10 digits without sign, fraction,
// exponent or leading zeros, such as "iter":300000.
inline bool jsonUintField(const char* json, const char* key, uint32_t* out) {
  if (!json || !key || !out) return false;
  const size_t key_length = strlen(key);
  for (const char* p = json; *p; ++p) {
    if (*p != '"' || strncmp(p + 1, key, key_length) != 0 ||
        p[1 + key_length] != '"') {
      continue;
    }
    const char* q = p + key_length + 2;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
    if (*q != ':') continue;
    ++q;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
    size_t digits = 0;
    uint64_t value = 0;
    while (q[digits] >= '0' && q[digits] <= '9') {
      if (digits == 10) return false;
      value = value * 10 + static_cast<uint64_t>(q[digits] - '0');
      ++digits;
    }
    const char next = q[digits];
    if (digits == 0 || (digits > 1 && q[0] == '0') || value > 0xffffffffULL ||
        !(next == ',' || next == '}' || next == ' ' || next == '\t' ||
          next == '\r' || next == '\n' || next == '\0')) {
      return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
  }
  return false;
}

// Stored NVS record: magic, version, flags, salt, key, PBKDF2 iterations and
// an FNV-1a checksum. Version 1 (a single SHA-256) is not accepted: its panels
// stay locked until the password is removed on the device and set again.
struct __attribute__((packed)) CredentialRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint8_t reserved[2];
  uint8_t salt[kSaltSize];
  uint8_t key[kKeySize];
  uint32_t iterations;
  uint32_t checksum;
};
static_assert(sizeof(CredentialRecord) == 64, "Web Admin credential record size");

constexpr uint32_t kRecordMagic = 0x41575448;  // "HTWA" little-endian
constexpr uint8_t kRecordVersion = 2;
constexpr uint8_t kRecordEnabled = 1U << 0;

inline uint32_t recordChecksum(const CredentialRecord& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(record) - sizeof(record.checksum); ++i) {
    hash = (hash ^ bytes[i]) * 16777619UL;
  }
  return hash;
}

inline CredentialRecord makeRecord(const Credential& credential) {
  CredentialRecord record;
  memset(&record, 0, sizeof(record));
  record.magic = kRecordMagic;
  record.version = kRecordVersion;
  record.flags = credential.enabled ? kRecordEnabled : 0;
  memcpy(record.salt, credential.salt, kSaltSize);
  memcpy(record.key, credential.key, kKeySize);
  record.iterations = credential.iterations;
  record.checksum = recordChecksum(record);
  return record;
}

inline bool applyRecord(const CredentialRecord& record, Credential& out) {
  if (record.magic != kRecordMagic || record.version != kRecordVersion ||
      record.checksum != recordChecksum(record) ||
      (record.flags & kRecordEnabled) == 0 ||
      !validIterations(record.iterations)) {
    return false;
  }
  out.enabled = true;
  memcpy(out.salt, record.salt, kSaltSize);
  memcpy(out.key, record.key, kKeySize);
  out.iterations = record.iterations;
  return true;
}

// Stored sessions, so a restart does not sign the browser out: the id hash,
// the CSRF token and the wall-clock expiry of each session that has one.
// Written only on login, logout and password changes.
struct __attribute__((packed)) SessionRecordEntry {
  uint8_t id_hash[ht_crypto::kSha256Size];
  uint8_t csrf[kTokenSize];
  uint32_t expires_epoch;
};

struct __attribute__((packed)) SessionRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t count;
  uint8_t reserved[2];
  SessionRecordEntry entries[kMaxSessions];
  uint32_t checksum;
};
static_assert(sizeof(SessionRecord) == 220, "Web Admin session record size");

constexpr uint32_t kSessionRecordMagic = 0x53575448;  // "HTWS" little-endian
constexpr uint8_t kSessionRecordVersion = 1;

inline uint32_t sessionRecordChecksum(const SessionRecord& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(record) - sizeof(record.checksum); ++i) {
    hash = (hash ^ bytes[i]) * 16777619UL;
  }
  return hash;
}

// Sessions from a login without a set clock have no expiry and stay in RAM.
inline SessionRecord makeSessionRecord(const Tables& tables) {
  SessionRecord record;
  memset(&record, 0, sizeof(record));
  record.magic = kSessionRecordMagic;
  record.version = kSessionRecordVersion;
  for (const SessionSlot& slot : tables.sessions) {
    if (!slot.used || !slot.expires_epoch) continue;
    SessionRecordEntry& entry = record.entries[record.count++];
    memcpy(entry.id_hash, slot.id_hash, sizeof(entry.id_hash));
    memcpy(entry.csrf, slot.csrf, kTokenSize);
    entry.expires_epoch = slot.expires_epoch;
  }
  record.checksum = sessionRecordChecksum(record);
  return record;
}

// Restores the stored sessions into empty tables. epoch_now is 0 while the
// clock is not set; expireSessions() drops outdated ones once it is.
inline size_t applySessionRecord(const SessionRecord& record, Tables& tables,
                                 uint32_t now, uint32_t epoch_now) {
  if (record.magic != kSessionRecordMagic ||
      record.version != kSessionRecordVersion || record.count > kMaxSessions ||
      record.checksum != sessionRecordChecksum(record)) {
    return 0;
  }
  const bool clock_set = epoch_now >= kMinValidEpoch;
  size_t restored = 0;
  for (size_t i = 0; i < record.count && restored < kMaxSessions; ++i) {
    const SessionRecordEntry& entry = record.entries[i];
    if (entry.expires_epoch < kMinValidEpoch) continue;
    if (clock_set && (epoch_now >= entry.expires_epoch ||
                      entry.expires_epoch - epoch_now > kSessionMaxSeconds)) {
      continue;
    }
    SessionSlot& slot = tables.sessions[restored++];
    memcpy(slot.id_hash, entry.id_hash, sizeof(slot.id_hash));
    memcpy(slot.csrf, entry.csrf, kTokenSize);
    slot.created_ms = now;
    slot.last_seen_ms = now;
    slot.expires_epoch = entry.expires_epoch;
    slot.used = true;
  }
  return restored;
}

}  // namespace web_admin_auth
