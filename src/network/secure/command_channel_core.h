#pragma once

// Encrypted, authenticated commands between a panel and the HomeTiles Bridge.
// The full protocol is described in docs-dev/command-encryption.md. This
// header holds the platform-independent parts (pairing messages and numbers,
// key derivation, envelope, message header and replay window), so the host tests in
// tools/tests/network/ run this exact code against the Python Bridge format.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/core/security/ht_crypto.h"

namespace command_channel {

// Contract v2: the pairing key K comes from an X25519 exchange that the user
// confirms by comparing a six-digit number on the panel and in Home Assistant
// (numeric comparison with a commitment, as in Bluetooth LE). X25519 itself
// runs in src/core/security/x25519.*; everything here is portable.
constexpr char kKdfSalt[] = "HomeTiles command pairing v2";
constexpr size_t kPairKeySize = 32;  // X25519 keys, shared secret and K
constexpr size_t kPairNonceSize = 16;
constexpr size_t kPairIdSize = 8;
constexpr size_t kPairIdHexSize = kPairIdSize * 2 + 1;
constexpr uint32_t kPairNumberModulus = 1000000;
// "123 456" and NUL.
constexpr size_t kPairNumberDisplaySize = 8;
constexpr size_t kPairReasonSize = 16;
// A commit with both hex fields is about 190 bytes; unknown fields are
// ignored, so leave room for a few.
constexpr size_t kMaxPairMessageLength = 512;

constexpr size_t kKeySize = ht_crypto::kAeadKeySize;
constexpr size_t kKeyIdSize = 8;
constexpr size_t kKeyIdHexSize = kKeyIdSize * 2 + 1;
constexpr size_t kSessionSize = 16;
constexpr size_t kSessionHexSize = kSessionSize * 2 + 1;
constexpr size_t kChallengeSize = 16;
constexpr size_t kMaxNameLength = 32;
constexpr size_t kMaxBodyLength = 2048;
// "<type> <session> <seq> <name>\n" plus the body.
constexpr size_t kMaxHeaderLength = 7 + 1 + 32 + 1 + 10 + 1 + kMaxNameLength + 1;
constexpr size_t kMaxPlaintextLength = kMaxHeaderLength + kMaxBodyLength;
// {"v":1,"k":"<16>","n":"<24>","d":"<hex of plaintext and tag>"}
constexpr size_t kEnvelopeOverhead = 40 + 16 + 24 + 2 * ht_crypto::kAeadTagSize;
constexpr size_t kMaxEnvelopeLength = kEnvelopeOverhead + 2 * kMaxPlaintextLength;
// Messages accepted behind the highest sequence number, for reordering
// between the panel's normal and priority publish lanes.
constexpr uint32_t kReplayWindow = 64;

struct Keys {
  uint8_t panel_to_bridge[kKeySize];
  uint8_t bridge_to_panel[kKeySize];
  // Signs the retained Bridge announcement (signAnnouncement()).
  uint8_t announce[kKeySize];
  char key_id[kKeyIdHexSize];
};

// c = SHA-256("HomeTiles pairing commit v2" || pk_b || pk_p || n_b). The
// Bridge commits to its nonce before it sees the panel's, so neither side can
// steer the number.
inline void pairingCommit(const uint8_t pk_b[kPairKeySize],
                          const uint8_t pk_p[kPairKeySize],
                          const uint8_t n_b[kPairNonceSize],
                          uint8_t out[ht_crypto::kSha256Size]) {
  static const char kLabel[] = "HomeTiles pairing commit v2";
  ht_crypto::Sha256 ctx;
  ht_crypto::sha256Init(ctx);
  ht_crypto::sha256Update(ctx, kLabel, sizeof(kLabel) - 1);
  ht_crypto::sha256Update(ctx, pk_b, kPairKeySize);
  ht_crypto::sha256Update(ctx, pk_p, kPairKeySize);
  ht_crypto::sha256Update(ctx, n_b, kPairNonceSize);
  ht_crypto::sha256Final(ctx, out);
}

// T = SHA-256("HomeTiles pairing v2" || u16be(len(base)) || base || pk_p ||
// pk_b || n_p || n_b). Binding the base topic stops a start of one panel
// from being answered for another.
inline bool pairingTranscript(const char* base, const uint8_t pk_p[kPairKeySize],
                              const uint8_t pk_b[kPairKeySize],
                              const uint8_t n_p[kPairNonceSize],
                              const uint8_t n_b[kPairNonceSize],
                              uint8_t out[ht_crypto::kSha256Size]) {
  const size_t base_length = base ? strlen(base) : 0;
  if (base_length == 0 || base_length > 0xffff) return false;
  static const char kLabel[] = "HomeTiles pairing v2";
  const uint8_t length_be[2] = {static_cast<uint8_t>(base_length >> 8),
                                static_cast<uint8_t>(base_length & 0xff)};
  ht_crypto::Sha256 ctx;
  ht_crypto::sha256Init(ctx);
  ht_crypto::sha256Update(ctx, kLabel, sizeof(kLabel) - 1);
  ht_crypto::sha256Update(ctx, length_be, sizeof(length_be));
  ht_crypto::sha256Update(ctx, base, base_length);
  ht_crypto::sha256Update(ctx, pk_p, kPairKeySize);
  ht_crypto::sha256Update(ctx, pk_b, kPairKeySize);
  ht_crypto::sha256Update(ctx, n_p, kPairNonceSize);
  ht_crypto::sha256Update(ctx, n_b, kPairNonceSize);
  ht_crypto::sha256Final(ctx, out);
  return true;
}

// The six-digit number both sides show: the first four bytes of
// SHA-256("HomeTiles pairing number v2" || T), big-endian, modulo 10^6.
inline uint32_t pairingNumber(const uint8_t transcript[ht_crypto::kSha256Size]) {
  static const char kLabel[] = "HomeTiles pairing number v2";
  uint8_t digest[ht_crypto::kSha256Size];
  ht_crypto::Sha256 ctx;
  ht_crypto::sha256Init(ctx);
  ht_crypto::sha256Update(ctx, kLabel, sizeof(kLabel) - 1);
  ht_crypto::sha256Update(ctx, transcript, ht_crypto::kSha256Size);
  ht_crypto::sha256Final(ctx, digest);
  const uint32_t value = (static_cast<uint32_t>(digest[0]) << 24) |
                         (static_cast<uint32_t>(digest[1]) << 16) |
                         (static_cast<uint32_t>(digest[2]) << 8) |
                         static_cast<uint32_t>(digest[3]);
  return value % kPairNumberModulus;
}

// Always six digits with leading zeros, grouped as "061 806".
inline void formatPairingNumber(uint32_t number, char out[kPairNumberDisplaySize]) {
  number %= kPairNumberModulus;
  snprintf(out, kPairNumberDisplaySize, "%03lu %03lu",
           static_cast<unsigned long>(number / 1000),
           static_cast<unsigned long>(number % 1000));
}

// K = HKDF-SHA256(salt = T, ikm = X25519 shared secret, info "pairing key").
inline void pairingKey(const uint8_t shared[kPairKeySize],
                       const uint8_t transcript[ht_crypto::kSha256Size],
                       uint8_t key[kPairKeySize]) {
  static const char kInfo[] = "pairing key";
  ht_crypto::hkdfSha256(transcript, ht_crypto::kSha256Size, shared, kPairKeySize,
                        reinterpret_cast<const uint8_t*>(kInfo), sizeof(kInfo) - 1,
                        key, kPairKeySize);
}

// m = HMAC-SHA256(K, "confirm panel" || T) or "confirm bridge" || T: proves
// the same key and that the user confirmed on that side.
inline void pairingConfirmation(const uint8_t key[kPairKeySize],
                                const uint8_t transcript[ht_crypto::kSha256Size],
                                bool from_panel,
                                uint8_t out[ht_crypto::kSha256Size]) {
  const char* label = from_panel ? "confirm panel" : "confirm bridge";
  ht_crypto::HmacSha256 mac;
  ht_crypto::hmacSha256Init(mac, key, kPairKeySize);
  ht_crypto::hmacSha256Update(mac, label, strlen(label));
  ht_crypto::hmacSha256Update(mac, transcript, ht_crypto::kSha256Size);
  ht_crypto::hmacSha256Final(mac, out);
  ht_crypto::secureZero(&mac, sizeof(mac));
}

// HKDF-SHA256 with the fixed salt and K as input; each key has its own info
// label. Everything after pairing uses these keys exactly as before.
inline void deriveKeys(const uint8_t pairing_key[kPairKeySize], Keys& keys) {
  const uint8_t* salt = reinterpret_cast<const uint8_t*>(kKdfSalt);
  auto expand = [&](const char* info, uint8_t* out, size_t length) {
    ht_crypto::hkdfSha256(salt, sizeof(kKdfSalt) - 1, pairing_key, kPairKeySize,
                          reinterpret_cast<const uint8_t*>(info), strlen(info),
                          out, length);
  };
  expand("panel-to-bridge", keys.panel_to_bridge, kKeySize);
  expand("bridge-to-panel", keys.bridge_to_panel, kKeySize);
  expand("announce", keys.announce, kKeySize);
  uint8_t key_id[kKeyIdSize];
  expand("key-id", key_id, sizeof(key_id));
  ht_crypto::hexEncode(key_id, sizeof(key_id), keys.key_id, sizeof(keys.key_id));
}

// Plain pairing messages on {base}/pair/panel and {base}/pair/bridge: a flat
// JSON object with "v":2, a type "t", the attempt "id" and hex or text fields.
enum class PairType : uint8_t { Start, Commit, Nonce, Confirm, Abort };

struct PairMessage {
  PairType type = PairType::Abort;
  char id[kPairIdHexSize] = {};
  bool has_pk = false;
  uint8_t pk[kPairKeySize] = {};
  bool has_c = false;
  uint8_t c[ht_crypto::kSha256Size] = {};
  bool has_n = false;
  uint8_t n[kPairNonceSize] = {};
  bool has_m = false;
  uint8_t m[ht_crypto::kSha256Size] = {};
  // Abort reason for display only: busy, rate, paired, rejected, timeout,
  // cancel, or anything else (a general abort).
  char reason[kPairReasonSize] = {};
};

// Finds "key": and returns its raw value: a quoted string of [a-z0-9_] or a
// run of digits. Every other character in a value is refused.
inline bool pairField(const char* json, size_t length, const char* key,
                      const char** value, size_t* value_length, bool* quoted) {
  const size_t key_length = strlen(key);
  for (size_t i = 0; i + key_length + 3 < length; ++i) {
    if (json[i] != '"' || memcmp(json + i + 1, key, key_length) != 0 ||
        json[i + 1 + key_length] != '"') {
      continue;
    }
    size_t p = i + key_length + 2;
    while (p < length && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= length || json[p] != ':') continue;
    ++p;
    while (p < length && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= length) return false;
    const bool is_string = json[p] == '"';
    if (is_string) ++p;
    const size_t start = p;
    while (p < length) {
      const char c = json[p];
      const bool digit = c >= '0' && c <= '9';
      const bool word = (c >= 'a' && c <= 'z') || c == '_';
      if (!(digit || (is_string && word))) break;
      ++p;
    }
    if (is_string && (p >= length || json[p] != '"')) return false;
    if (p == start) return false;
    *value = json + start;
    *value_length = p - start;
    *quoted = is_string;
    return true;
  }
  return false;
}

// Lowercase hex of exactly out_size bytes.
inline bool pairHexField(const char* json, size_t length, const char* key,
                         uint8_t* out, size_t out_size) {
  const char* value = nullptr;
  size_t value_length = 0;
  bool quoted = false;
  if (!pairField(json, length, key, &value, &value_length, &quoted) || !quoted ||
      value_length != 2 * out_size) {
    return false;
  }
  for (size_t i = 0; i < value_length; ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return ht_crypto::hexDecode(value, value_length, out, out_size);
}

inline bool parsePairMessage(const char* json, size_t length, PairMessage& message) {
  if (!json || length < 2 || length > kMaxPairMessageLength || json[0] != '{' ||
      json[length - 1] != '}') {
    return false;
  }
  const char* value = nullptr;
  size_t value_length = 0;
  bool quoted = false;
  if (!pairField(json, length, "v", &value, &value_length, &quoted) || quoted ||
      value_length != 1 || value[0] != '2') {
    return false;
  }
  if (!pairField(json, length, "t", &value, &value_length, &quoted) || !quoted) {
    return false;
  }
  PairMessage parsed;
  auto is = [&](const char* name) {
    return value_length == strlen(name) && memcmp(value, name, value_length) == 0;
  };
  if (is("start")) parsed.type = PairType::Start;
  else if (is("commit")) parsed.type = PairType::Commit;
  else if (is("nonce")) parsed.type = PairType::Nonce;
  else if (is("confirm")) parsed.type = PairType::Confirm;
  else if (is("abort")) parsed.type = PairType::Abort;
  else return false;
  uint8_t id[kPairIdSize];
  if (!pairHexField(json, length, "id", id, sizeof(id))) return false;
  ht_crypto::hexEncode(id, sizeof(id), parsed.id, sizeof(parsed.id));
  parsed.has_pk = pairHexField(json, length, "pk", parsed.pk, sizeof(parsed.pk));
  parsed.has_c = pairHexField(json, length, "c", parsed.c, sizeof(parsed.c));
  parsed.has_n = pairHexField(json, length, "n", parsed.n, sizeof(parsed.n));
  parsed.has_m = pairHexField(json, length, "m", parsed.m, sizeof(parsed.m));
  if (pairField(json, length, "r", &value, &value_length, &quoted) && quoted &&
      value_length < sizeof(parsed.reason)) {
    memcpy(parsed.reason, value, value_length);
    parsed.reason[value_length] = '\0';
  }
  switch (parsed.type) {
    case PairType::Start:
      if (!parsed.has_pk) return false;
      break;
    case PairType::Commit:
      if (!parsed.has_pk || !parsed.has_c) return false;
      break;
    case PairType::Nonce:
      if (!parsed.has_n) return false;
      break;
    case PairType::Confirm:
      if (!parsed.has_m) return false;
      break;
    case PairType::Abort:
      break;
  }
  message = parsed;
  return true;
}

// Builds {"v":2,"t":"<type>","id":"<id>"[,"<field>":"<hex or text>"]}.
// Returns the length, or 0 when out is too small.
inline size_t buildPairMessage(const char* type, const char* id, const char* field,
                               const uint8_t* bytes, size_t bytes_length,
                               const char* text, char* out, size_t out_size) {
  if (!type || !id || !out) return 0;
  int written = snprintf(out, out_size, "{\"v\":2,\"t\":\"%s\",\"id\":\"%s\"", type, id);
  if (written <= 0 || static_cast<size_t>(written) >= out_size) return 0;
  size_t offset = static_cast<size_t>(written);
  if (field && (bytes || text)) {
    written = snprintf(out + offset, out_size - offset, ",\"%s\":\"", field);
    if (written <= 0 || offset + written >= out_size) return 0;
    offset += static_cast<size_t>(written);
    if (bytes) {
      if (offset + 2 * bytes_length + 1 > out_size) return 0;
      ht_crypto::hexEncode(bytes, bytes_length, out + offset, out_size - offset);
      offset += 2 * bytes_length;
    } else {
      const size_t text_length = strlen(text);
      if (offset + text_length + 1 > out_size) return 0;
      memcpy(out + offset, text, text_length);
      offset += text_length;
    }
    if (offset + 1 >= out_size) return 0;
    out[offset++] = '"';
  }
  if (offset + 2 > out_size) return 0;
  out[offset++] = '}';
  out[offset] = '\0';
  return offset;
}

// Unpair ends the pairing on both sides: whichever side removes it tells the
// other one inside the current session, numbered like cmd/data.
enum class MessageType : uint8_t { Hello, Session, Rekey, Command, Data, Unpair };

inline const char* typeName(MessageType type) {
  switch (type) {
    case MessageType::Hello: return "hello";
    case MessageType::Session: return "session";
    case MessageType::Rekey: return "rekey";
    case MessageType::Command: return "cmd";
    case MessageType::Data: return "data";
    case MessageType::Unpair: return "unpair";
  }
  return "";
}

struct Header {
  MessageType type = MessageType::Hello;
  bool has_session = false;
  uint8_t session[kSessionSize] = {};
  uint32_t seq = 0;
  char name[kMaxNameLength + 1] = {};
};

inline bool validName(const char* name) {
  const size_t length = name ? strlen(name) : 0;
  if (length == 0 || length > kMaxNameLength) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

// Plaintext: "<type> <session hex|-> <seq> <name|->\n<body>".
inline size_t buildPlaintext(const Header& header, const uint8_t* body,
                             size_t body_length, uint8_t* out,
                             size_t out_size) {
  if (!out || body_length > kMaxBodyLength || (!body && body_length)) return 0;
  if (header.name[0] && !validName(header.name)) return 0;
  char session[kSessionHexSize] = "-";
  if (header.has_session) {
    ht_crypto::hexEncode(header.session, kSessionSize, session, sizeof(session));
  }
  const int written = snprintf(reinterpret_cast<char*>(out), out_size,
                               "%s %s %lu %s\n", typeName(header.type), session,
                               static_cast<unsigned long>(header.seq),
                               header.name[0] ? header.name : "-");
  if (written <= 0 || static_cast<size_t>(written) + body_length > out_size) {
    return 0;
  }
  if (body_length) memcpy(out + written, body, body_length);
  return static_cast<size_t>(written) + body_length;
}

inline bool parsePlaintext(const uint8_t* plaintext, size_t length,
                           Header& header, const uint8_t** body,
                           size_t* body_length) {
  if (!plaintext || !body || !body_length) return false;
  const uint8_t* newline =
      static_cast<const uint8_t*>(memchr(plaintext, '\n', length));
  if (!newline) return false;
  const size_t line_length = static_cast<size_t>(newline - plaintext);
  if (line_length > kMaxHeaderLength) return false;
  char line[kMaxHeaderLength + 1];
  memcpy(line, plaintext, line_length);
  line[line_length] = '\0';

  // Exactly four fields separated by single spaces; the last keeps any rest,
  // so a fifth field fails the name check below.
  char* fields[4] = {};
  size_t count = 0;
  char* cursor = line;
  while (count < 4) {
    fields[count++] = cursor;
    if (count == 4) break;
    char* space = strchr(cursor, ' ');
    if (!space) break;
    *space = '\0';
    cursor = space + 1;
  }
  if (count != 4 || strchr(fields[3], ' ')) return false;

  Header parsed;
  if (strcmp(fields[0], "hello") == 0) parsed.type = MessageType::Hello;
  else if (strcmp(fields[0], "session") == 0) parsed.type = MessageType::Session;
  else if (strcmp(fields[0], "rekey") == 0) parsed.type = MessageType::Rekey;
  else if (strcmp(fields[0], "cmd") == 0) parsed.type = MessageType::Command;
  else if (strcmp(fields[0], "data") == 0) parsed.type = MessageType::Data;
  else if (strcmp(fields[0], "unpair") == 0) parsed.type = MessageType::Unpair;
  else return false;

  if (strcmp(fields[1], "-") != 0) {
    if (!ht_crypto::hexDecode(fields[1], strlen(fields[1]), parsed.session,
                              kSessionSize)) {
      return false;
    }
    parsed.has_session = true;
  }
  const size_t seq_length = strlen(fields[2]);
  if (seq_length == 0 || seq_length > 10) return false;
  uint64_t seq = 0;
  for (size_t i = 0; i < seq_length; ++i) {
    if (fields[2][i] < '0' || fields[2][i] > '9') return false;
    seq = seq * 10 + static_cast<uint64_t>(fields[2][i] - '0');
  }
  if (seq > 0xffffffffULL || (seq_length > 1 && fields[2][0] == '0')) return false;
  parsed.seq = static_cast<uint32_t>(seq);
  if (strcmp(fields[3], "-") != 0) {
    if (!validName(fields[3])) return false;
    strcpy(parsed.name, fields[3]);
  }
  const size_t remaining = length - line_length - 1;
  if (remaining > kMaxBodyLength) return false;
  header = parsed;
  *body = newline + 1;
  *body_length = remaining;
  return true;
}

// Returns the envelope length, or 0 when out is too small.
inline size_t sealEnvelope(const uint8_t key[kKeySize],
                           const char key_id[kKeyIdHexSize],
                           const uint8_t nonce[ht_crypto::kAeadNonceSize],
                           const char* topic, const uint8_t* plaintext,
                           size_t length, uint8_t* scratch, char* out,
                           size_t out_size) {
  // scratch holds the ciphertext and tag: length + 16 bytes.
  if (!key || !key_id || !nonce || !topic || !scratch || !out ||
      length > kMaxPlaintextLength) {
    return 0;
  }
  const size_t needed = 36 + (kKeyIdHexSize - 1) + 2 * ht_crypto::kAeadNonceSize +
                        2 * (length + ht_crypto::kAeadTagSize) + 1;
  if (out_size < needed) return 0;
  ht_crypto::chacha20Poly1305Seal(key, nonce,
                                  reinterpret_cast<const uint8_t*>(topic),
                                  strlen(topic), plaintext, length, scratch,
                                  scratch + length);
  char nonce_hex[2 * ht_crypto::kAeadNonceSize + 1];
  ht_crypto::hexEncode(nonce, ht_crypto::kAeadNonceSize, nonce_hex,
                       sizeof(nonce_hex));
  int written = snprintf(out, out_size, "{\"v\":1,\"k\":\"%s\",\"n\":\"%s\",\"d\":\"",
                         key_id, nonce_hex);
  if (written <= 0) return 0;
  size_t offset = static_cast<size_t>(written);
  ht_crypto::hexEncode(scratch, length + ht_crypto::kAeadTagSize, out + offset,
                       out_size - offset);
  offset += 2 * (length + ht_crypto::kAeadTagSize);
  if (offset + 3 > out_size) return 0;
  out[offset++] = '"';
  out[offset++] = '}';
  out[offset] = '\0';
  return offset;
}

// Finds "key":"<value>" in the flat envelope object; values are hex only.
inline bool envelopeField(const char* json, size_t length, const char* key,
                          const char** value, size_t* value_length) {
  const size_t key_length = strlen(key);
  for (size_t i = 0; i + key_length + 3 < length; ++i) {
    if (json[i] != '"' || memcmp(json + i + 1, key, key_length) != 0 ||
        json[i + 1 + key_length] != '"') {
      continue;
    }
    size_t p = i + key_length + 2;
    while (p < length && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= length || json[p] != ':') continue;
    ++p;
    while (p < length && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= length || json[p] != '"') return false;
    const size_t start = ++p;
    while (p < length && json[p] != '"') {
      const char c = json[p];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))) {
        return false;
      }
      ++p;
    }
    if (p >= length) return false;
    *value = json + start;
    *value_length = p - start;
    return true;
  }
  return false;
}

enum class OpenResult : uint8_t { Ok, Malformed, OtherKey, Rejected };

// Decrypts into plaintext_out (kMaxPlaintextLength bytes). scratch needs
// kMaxPlaintextLength + 16 bytes.
inline OpenResult openEnvelope(const uint8_t key[kKeySize],
                               const char key_id[kKeyIdHexSize],
                               const char* topic, const char* envelope,
                               size_t envelope_length, uint8_t* scratch,
                               uint8_t* plaintext_out, size_t* plaintext_length) {
  if (!envelope || !topic || !scratch || !plaintext_out || !plaintext_length ||
      envelope_length > kMaxEnvelopeLength) {
    return OpenResult::Malformed;
  }
  const char* kid = nullptr;
  const char* nonce_hex = nullptr;
  const char* data = nullptr;
  size_t kid_length = 0, nonce_length = 0, data_length = 0;
  if (!envelopeField(envelope, envelope_length, "k", &kid, &kid_length) ||
      !envelopeField(envelope, envelope_length, "n", &nonce_hex, &nonce_length) ||
      !envelopeField(envelope, envelope_length, "d", &data, &data_length) ||
      kid_length != kKeyIdHexSize - 1 ||
      nonce_length != 2 * ht_crypto::kAeadNonceSize || data_length % 2 ||
      data_length < 2 * ht_crypto::kAeadTagSize ||
      data_length > 2 * (kMaxPlaintextLength + ht_crypto::kAeadTagSize)) {
    return OpenResult::Malformed;
  }
  uint8_t kid_bytes[kKeyIdSize];
  uint8_t own_kid[kKeyIdSize];
  if (!ht_crypto::hexDecode(kid, kid_length, kid_bytes, sizeof(kid_bytes)) ||
      !ht_crypto::hexDecode(key_id, kKeyIdHexSize - 1, own_kid, sizeof(own_kid))) {
    return OpenResult::Malformed;
  }
  if (!ht_crypto::equalConstantTime(kid_bytes, own_kid, kKeyIdSize)) {
    return OpenResult::OtherKey;
  }
  uint8_t nonce[ht_crypto::kAeadNonceSize];
  const size_t sealed_length = data_length / 2;
  if (!ht_crypto::hexDecode(nonce_hex, nonce_length, nonce, sizeof(nonce)) ||
      !ht_crypto::hexDecode(data, data_length, scratch, sealed_length)) {
    return OpenResult::Malformed;
  }
  const size_t length = sealed_length - ht_crypto::kAeadTagSize;
  if (!ht_crypto::chacha20Poly1305Open(
          key, nonce, reinterpret_cast<const uint8_t*>(topic), strlen(topic),
          scratch, length, scratch + length, plaintext_out)) {
    return OpenResult::Rejected;
  }
  *plaintext_length = length;
  return OpenResult::Ok;
}

// Sliding replay window over the per-session sequence numbers.
struct ReplayWindow {
  uint32_t highest = 0;
  uint64_t seen = 0;  // Bit n: highest - n was accepted.
};

inline bool acceptSequence(ReplayWindow& window, uint32_t seq) {
  if (seq == 0) return false;
  if (seq > window.highest) {
    const uint32_t shift = seq - window.highest;
    window.seen = shift >= 64 ? 0 : window.seen << shift;
    window.seen |= 1;
    window.highest = seq;
    return true;
  }
  const uint32_t offset = window.highest - seq;
  if (offset >= kReplayWindow) return false;
  const uint64_t bit = 1ULL << offset;
  if (window.seen & bit) return false;
  window.seen |= bit;
  return true;
}

// Stored pairing: K, from which the keys are derived again at boot. Pairing
// is either off (no record) or active; a v1 record (40 bytes, a typed code)
// is discarded and the panel runs unencrypted until it is paired again.
enum class PairingState : uint8_t { Off = 0, Active = 2 };

struct __attribute__((packed)) PairingRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t state;
  uint8_t reserved[2];
  uint8_t key[kPairKeySize];
  uint32_t checksum;
};
static_assert(sizeof(PairingRecord) == 44, "Pairing record size");

constexpr uint32_t kRecordMagic = 0x43435448;  // "HTCC" little-endian
constexpr uint8_t kRecordVersion = 2;
constexpr size_t kLegacyRecordSize = 40;

inline uint32_t recordChecksum(const PairingRecord& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(record) - sizeof(record.checksum); ++i) {
    hash = (hash ^ bytes[i]) * 16777619UL;
  }
  return hash;
}

inline PairingRecord makeRecord(const uint8_t key[kPairKeySize]) {
  PairingRecord record;
  memset(&record, 0, sizeof(record));
  record.magic = kRecordMagic;
  record.version = kRecordVersion;
  record.state = static_cast<uint8_t>(PairingState::Active);
  memcpy(record.key, key, kPairKeySize);
  record.checksum = recordChecksum(record);
  return record;
}

inline bool applyRecord(const PairingRecord& record, uint8_t key[kPairKeySize]) {
  if (record.magic != kRecordMagic || record.version != kRecordVersion ||
      record.checksum != recordChecksum(record) ||
      record.state != static_cast<uint8_t>(PairingState::Active)) {
    return false;
  }
  memcpy(key, record.key, kPairKeySize);
  return true;
}

// Signed announcement on tab5_lvgl/config/{id}/bridge. The member
// ,"sig":"<64 hex>" goes before the final '}', with
// sig = HMAC-SHA256(announce key, topic "\n" unsigned payload), so the Bridge
// verifies the exact bytes without re-serializing JSON. Returns the signed
// length (payload length + 73), or 0 when the payload is not a JSON object
// or out is too small.
constexpr size_t kAnnouncementSignatureOverhead = 8 + 2 * ht_crypto::kSha256Size + 1;

inline size_t signAnnouncement(const uint8_t key[kKeySize], const char* topic,
                               const char* payload, size_t length, char* out,
                               size_t out_size) {
  if (!key || !topic || !payload || !out || length < 2 || payload[0] != '{' ||
      payload[length - 1] != '}' ||
      out_size < length + kAnnouncementSignatureOverhead + 1) {
    return 0;
  }
  ht_crypto::HmacSha256 mac;
  ht_crypto::hmacSha256Init(mac, key, kKeySize);
  ht_crypto::hmacSha256Update(mac, topic, strlen(topic));
  ht_crypto::hmacSha256Update(mac, "\n", 1);
  ht_crypto::hmacSha256Update(mac, payload, length);
  uint8_t digest[ht_crypto::kSha256Size];
  ht_crypto::hmacSha256Final(mac, digest);
  ht_crypto::secureZero(&mac, sizeof(mac));
  size_t offset = length - 1;
  memcpy(out, payload, offset);
  memcpy(out + offset, ",\"sig\":\"", 8);
  offset += 8;
  ht_crypto::hexEncode(digest, sizeof(digest), out + offset, out_size - offset);
  offset += 2 * sizeof(digest);
  out[offset++] = '"';
  out[offset++] = '}';
  out[offset] = '\0';
  ht_crypto::secureZero(digest, sizeof(digest));
  return offset;
}

// Panel-to-Bridge command topics that travel sealed while pairing is active:
// {base}/cmnd/<leaf>. Returns the leaf, or nullptr for any other topic.
inline const char* sealedCommandLeaf(const char* topic, const char* base,
                                     size_t base_length) {
  static const char* const kLeaves[] = {"scene", "light", "switch", "media",
                                        "climate", "cover", "camera", "value"};
  if (!topic || !base || strncmp(topic, base, base_length) != 0 ||
      strncmp(topic + base_length, "/cmnd/", 6) != 0) {
    return nullptr;
  }
  const char* leaf = topic + base_length + 6;
  for (const char* candidate : kLeaves) {
    if (strcmp(leaf, candidate) == 0) return candidate;
  }
  return nullptr;
}

}  // namespace command_channel
