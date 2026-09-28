#pragma once

// Encrypted, authenticated commands between a panel and the HomeTiles Bridge.
// The full protocol is described in docs-dev/command-encryption.md. This
// header holds the platform-independent parts (pairing code, key derivation,
// envelope, message header and replay window), so the host tests in
// tools/tests/network/ run this exact code against the Python Bridge format.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/core/security/ht_crypto.h"

namespace command_channel {

// Pairing code: 25 Crockford Base32 symbols (125 random bits), shown as five
// groups of five. The Bridge accepts either case, spaces and dashes, and the
// usual O/0 and I/L/1 confusions.
constexpr size_t kCodeLength = 25;
constexpr size_t kCodeDisplaySize = kCodeLength + 4 + 1;
constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr char kKdfSalt[] = "HomeTiles command pairing v1";

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

inline void generateCode(const uint8_t random[kCodeLength],
                         char code[kCodeLength + 1]) {
  for (size_t i = 0; i < kCodeLength; ++i) code[i] = kAlphabet[random[i] & 31];
  code[kCodeLength] = '\0';
}

inline int symbolValue(char c) {
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  if (c == 'O') c = '0';
  if (c == 'I' || c == 'L') c = '1';
  for (int i = 0; i < 32; ++i) {
    if (kAlphabet[i] == c) return i;
  }
  return -1;
}

// Canonical form: 25 upper-case alphabet symbols. Separators are dropped.
inline bool normalizeCode(const char* input, char out[kCodeLength + 1]) {
  if (!input) return false;
  size_t length = 0;
  for (const char* p = input; *p; ++p) {
    if (*p == '-' || *p == ' ' || *p == '\t') continue;
    const int value = symbolValue(*p);
    if (value < 0 || length >= kCodeLength) return false;
    out[length++] = kAlphabet[value];
  }
  out[length] = '\0';
  return length == kCodeLength;
}

inline void formatCode(const char code[kCodeLength + 1],
                       char out[kCodeDisplaySize]) {
  size_t o = 0;
  for (size_t i = 0; i < kCodeLength; ++i) {
    if (i && i % 5 == 0) out[o++] = '-';
    out[o++] = code[i];
  }
  out[o] = '\0';
}

// HKDF-SHA256 with the fixed salt; each key has its own info label.
inline bool deriveKeys(const char code[kCodeLength + 1], Keys& keys) {
  char canonical[kCodeLength + 1];
  if (!normalizeCode(code, canonical)) return false;
  const uint8_t* salt = reinterpret_cast<const uint8_t*>(kKdfSalt);
  const uint8_t* ikm = reinterpret_cast<const uint8_t*>(canonical);
  auto expand = [&](const char* info, uint8_t* out, size_t length) {
    ht_crypto::hkdfSha256(salt, sizeof(kKdfSalt) - 1, ikm, kCodeLength,
                          reinterpret_cast<const uint8_t*>(info), strlen(info),
                          out, length);
  };
  expand("panel-to-bridge", keys.panel_to_bridge, kKeySize);
  expand("bridge-to-panel", keys.bridge_to_panel, kKeySize);
  expand("announce", keys.announce, kKeySize);
  uint8_t key_id[kKeyIdSize];
  expand("key-id", key_id, sizeof(key_id));
  ht_crypto::hexEncode(key_id, sizeof(key_id), keys.key_id, sizeof(keys.key_id));
  ht_crypto::secureZero(canonical, sizeof(canonical));
  return true;
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

// Stored pairing: the code (so the display can show it) and the state. The
// keys are derived again at boot.
enum class PairingState : uint8_t { Off = 0, Pending = 1, Active = 2 };

struct __attribute__((packed)) PairingRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t state;
  uint8_t reserved[2];
  char code[kCodeLength + 1];
  uint8_t padding[2];
  uint32_t checksum;
};
static_assert(sizeof(PairingRecord) == 40, "Pairing record size");

constexpr uint32_t kRecordMagic = 0x43435448;  // "HTCC" little-endian
constexpr uint8_t kRecordVersion = 1;

inline uint32_t recordChecksum(const PairingRecord& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(record) - sizeof(record.checksum); ++i) {
    hash = (hash ^ bytes[i]) * 16777619UL;
  }
  return hash;
}

inline PairingRecord makeRecord(PairingState state, const char code[kCodeLength + 1]) {
  PairingRecord record;
  memset(&record, 0, sizeof(record));
  record.magic = kRecordMagic;
  record.version = kRecordVersion;
  record.state = static_cast<uint8_t>(state);
  memcpy(record.code, code, kCodeLength);
  record.checksum = recordChecksum(record);
  return record;
}

inline bool applyRecord(const PairingRecord& record, PairingState& state,
                        char code[kCodeLength + 1]) {
  if (record.magic != kRecordMagic || record.version != kRecordVersion ||
      record.checksum != recordChecksum(record) ||
      (record.state != static_cast<uint8_t>(PairingState::Pending) &&
       record.state != static_cast<uint8_t>(PairingState::Active))) {
    return false;
  }
  char stored[kCodeLength + 1];
  memcpy(stored, record.code, kCodeLength);
  stored[kCodeLength] = '\0';
  if (!normalizeCode(stored, code) || strcmp(stored, code) != 0) return false;
  state = static_cast<PairingState>(record.state);
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
