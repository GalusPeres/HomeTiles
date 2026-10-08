#pragma once

// Direct link between a panel and the HomeTiles Bridge (docs-dev/bridge-link.md).
// This header holds the platform-independent parts: frames, the hello and
// welcome messages, the session keys and sealed frames. The host test
// tools/tests/network/test-bridge-link-core.mjs runs this exact code against
// an independent Node implementation and the vectors the Bridge tests share.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/core/security/ht_crypto.h"

namespace bridge_link {

constexpr int kVersion = 1;
// Frame body limit (type, payload and tag) and the parts of a publish.
constexpr size_t kMaxFrameLength = 70000;
constexpr size_t kHeaderSize = 4;
constexpr size_t kTagSize = ht_crypto::kAeadTagSize;
constexpr size_t kMaxTopicLength = 255;
constexpr size_t kMaxPayloadLength = 65535;
constexpr size_t kMaxStreamChunk = 8192;
constexpr size_t kMaxStreamLength = 2u * 1024u * 1024u;
constexpr size_t kNonceSize = 16;
constexpr size_t kKeySize = ht_crypto::kAeadKeySize;
constexpr size_t kMaxDeviceIdLength = 32;
constexpr size_t kMaxBaseLength = 128;
constexpr size_t kMaxHelloLength = 512;
constexpr size_t kReasonSize = 16;

// Timing of the panel side.
constexpr uint32_t kHandshakeTimeoutMs = 10000;
constexpr uint32_t kPingIdleMs = 15000;
constexpr uint32_t kIdleTimeoutMs = 45000;

enum FrameType : uint8_t {
  kHello = 0x01,
  kWelcome = 0x02,
  kRefuse = 0x03,
  kReady = 0x04,
  kPublish = 0x10,
  kSubscribe = 0x11,
  kUnsubscribe = 0x12,
  kStreamBegin = 0x13,
  kStreamData = 0x14,
  kStreamEnd = 0x15,
  kPing = 0x20,
  kPong = 0x21,
};

constexpr uint8_t kFlagRetain = 0x01;

inline void putU16(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value >> 8);
  out[1] = static_cast<uint8_t>(value);
}

inline void putU32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value >> 24);
  out[1] = static_cast<uint8_t>(value >> 16);
  out[2] = static_cast<uint8_t>(value >> 8);
  out[3] = static_cast<uint8_t>(value);
}

inline uint32_t getU32(const uint8_t* in) {
  return (static_cast<uint32_t>(in[0]) << 24) | (static_cast<uint32_t>(in[1]) << 16) |
         (static_cast<uint32_t>(in[2]) << 8) | in[3];
}

// Body length of a frame header; 0 when it is outside 1..kMaxFrameLength.
inline size_t frameLength(const uint8_t header[kHeaderSize]) {
  const uint32_t length = getU32(header);
  return length >= 1 && length <= kMaxFrameLength ? length : 0;
}

// A concrete topic: 1..255 bytes without NUL or MQTT wildcards.
inline bool validTopic(const char* topic, size_t length) {
  if (!topic || length == 0 || length > kMaxTopicLength) return false;
  for (size_t i = 0; i < length; ++i) {
    if (topic[i] == '\0' || topic[i] == '+' || topic[i] == '#') return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Handshake messages

// {"v":1,"id":"<id>","base":"<base>","mode":"session","kid":"<16 hex>","n":"<32 hex>"}
// or, without a key (pairing), {"v":1,"id":...,"base":...,"mode":"pair"}.
// Device id and base are written unescaped and must not need escaping.
// stream_limit: the largest message the panel takes from the Bridge as a
// stream (above kMaxPayloadLength, at most kMaxStreamLength), sent as
// ,"rx":<bytes> in session mode; 0 leaves it out and the Bridge streams
// nothing to the panel. Older Bridges ignore the field.
inline size_t buildHello(const char* device_id, const char* base, const char* key_id,
                         const uint8_t nonce[kNonceSize], char* out, size_t out_size,
                         size_t stream_limit = 0) {
  if (!device_id || !base || !out || !*device_id || !*base ||
      strlen(device_id) > kMaxDeviceIdLength || strlen(base) > kMaxBaseLength) {
    return 0;
  }
  for (const char* p = device_id; *p; ++p) {
    const char c = *p;
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) {
      return 0;
    }
  }
  if (!validTopic(base, strlen(base))) return 0;
  for (const char* p = base; *p; ++p) {
    if (*p == '"' || *p == '\\' || static_cast<uint8_t>(*p) < 0x20) return 0;
  }
  int written = 0;
  if (key_id) {
    char nonce_hex[2 * kNonceSize + 1];
    if (!nonce || strlen(key_id) != 16 ||
        !ht_crypto::hexEncode(nonce, kNonceSize, nonce_hex, sizeof(nonce_hex))) {
      return 0;
    }
    if (stream_limit && (stream_limit <= kMaxPayloadLength || stream_limit > kMaxStreamLength)) {
      return 0;
    }
    char rx[24] = "";
    if (stream_limit) snprintf(rx, sizeof(rx), ",\"rx\":%u", static_cast<unsigned>(stream_limit));
    written = snprintf(out, out_size,
                       "{\"v\":1,\"id\":\"%s\",\"base\":\"%s\",\"mode\":\"session\",\"kid\":\"%s\",\"n\":\"%s\"%s}",
                       device_id, base, key_id, nonce_hex, rx);
  } else {
    written = snprintf(out, out_size, "{\"v\":1,\"id\":\"%s\",\"base\":\"%s\",\"mode\":\"pair\"}",
                       device_id, base);
  }
  if (written <= 0 || static_cast<size_t>(written) >= out_size ||
      static_cast<size_t>(written) > kMaxHelloLength) {
    return 0;
  }
  return static_cast<size_t>(written);
}

// Finds "key":"<value>" in a flat JSON object the Bridge built; the value is
// returned without quotes (no escapes are expected in these messages).
inline bool findStringField(const char* json, size_t length, const char* key,
                            const char** value, size_t* value_length) {
  char pattern[24];
  const int pattern_length = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  if (pattern_length <= 0 || static_cast<size_t>(pattern_length) >= sizeof(pattern)) return false;
  for (size_t i = 0; i + static_cast<size_t>(pattern_length) <= length; ++i) {
    if (memcmp(json + i, pattern, static_cast<size_t>(pattern_length)) != 0) continue;
    const char* start = json + i + pattern_length;
    const char* end = static_cast<const char*>(memchr(start, '"', length - (start - json)));
    if (!end) return false;
    *value = start;
    *value_length = static_cast<size_t>(end - start);
    return true;
  }
  return false;
}

inline bool versionIsOne(const char* json, size_t length) {
  static const char kField[] = "\"v\":1";
  for (size_t i = 0; i + sizeof(kField) - 1 <= length; ++i) {
    if (memcmp(json + i, kField, sizeof(kField) - 1) != 0) continue;
    const size_t next = i + sizeof(kField) - 1;
    return next < length && (json[next] == ',' || json[next] == '}');
  }
  return false;
}

// Welcome: {"v":1,"n":"<32 hex>"} in session mode, {"v":1} in pair mode.
inline bool parseWelcome(const char* json, size_t length, bool expect_nonce,
                         uint8_t nonce[kNonceSize]) {
  if (!json || length < 2 || json[0] != '{' || json[length - 1] != '}' ||
      !versionIsOne(json, length)) {
    return false;
  }
  if (!expect_nonce) return true;
  const char* value = nullptr;
  size_t value_length = 0;
  return findStringField(json, length, "n", &value, &value_length) &&
         value_length == 2 * kNonceSize &&
         ht_crypto::hexDecode(value, value_length, nonce, kNonceSize);
}

// Refuse: {"v":1,"r":"<reason>"}; an unreadable one reads "invalid".
inline void parseRefuse(const char* json, size_t length, char reason[kReasonSize]) {
  const char* value = nullptr;
  size_t value_length = 0;
  if (json && findStringField(json, length, "r", &value, &value_length) && value_length > 0 &&
      value_length < kReasonSize) {
    for (size_t i = 0; i < value_length; ++i) {
      const char c = value[i];
      if (!((c >= 'a' && c <= 'z') || c == '_')) {
        value_length = 0;
        break;
      }
    }
  } else {
    value_length = 0;
  }
  if (value_length == 0) {
    strcpy(reason, "invalid");
    return;
  }
  memcpy(reason, value, value_length);
  reason[value_length] = '\0';
}

// ---------------------------------------------------------------------------
// Session keys and sealed frames

// salt = SHA-256("HomeTiles link v1" || u16be(len(id)) || id
//                || u16be(len(base)) || base || n_p || n_b)
inline void sessionSalt(const char* device_id, const char* base, const uint8_t n_p[kNonceSize],
                        const uint8_t n_b[kNonceSize], uint8_t out[ht_crypto::kSha256Size]) {
  static const char kLabel[] = "HomeTiles link v1";
  uint8_t length[2];
  ht_crypto::Sha256 ctx;
  ht_crypto::sha256Init(ctx);
  ht_crypto::sha256Update(ctx, kLabel, sizeof(kLabel) - 1);
  const size_t id_length = strlen(device_id);
  putU16(length, static_cast<uint32_t>(id_length));
  ht_crypto::sha256Update(ctx, length, sizeof(length));
  ht_crypto::sha256Update(ctx, device_id, id_length);
  const size_t base_length = strlen(base);
  putU16(length, static_cast<uint32_t>(base_length));
  ht_crypto::sha256Update(ctx, length, sizeof(length));
  ht_crypto::sha256Update(ctx, base, base_length);
  ht_crypto::sha256Update(ctx, n_p, kNonceSize);
  ht_crypto::sha256Update(ctx, n_b, kNonceSize);
  ht_crypto::sha256Final(ctx, out);
}

inline void sessionKeys(const uint8_t pairing_key[kKeySize], const char* device_id,
                        const char* base, const uint8_t n_p[kNonceSize],
                        const uint8_t n_b[kNonceSize], uint8_t panel_key[kKeySize],
                        uint8_t bridge_key[kKeySize]) {
  static const char kPanelInfo[] = "HomeTiles link panel-to-bridge v1";
  static const char kBridgeInfo[] = "HomeTiles link bridge-to-panel v1";
  uint8_t salt[ht_crypto::kSha256Size];
  sessionSalt(device_id, base, n_p, n_b, salt);
  ht_crypto::hkdfSha256(salt, sizeof(salt), pairing_key, kKeySize,
                        reinterpret_cast<const uint8_t*>(kPanelInfo), sizeof(kPanelInfo) - 1,
                        panel_key, kKeySize);
  ht_crypto::hkdfSha256(salt, sizeof(salt), pairing_key, kKeySize,
                        reinterpret_cast<const uint8_t*>(kBridgeInfo), sizeof(kBridgeInfo) - 1,
                        bridge_key, kKeySize);
  ht_crypto::secureZero(salt, sizeof(salt));
}

inline void counterNonce(uint64_t counter, uint8_t nonce[ht_crypto::kAeadNonceSize]) {
  memset(nonce, 0, 4);
  for (int i = 0; i < 8; ++i) {
    nonce[4 + i] = static_cast<uint8_t>(counter >> (56 - 8 * i));
  }
}

// Frame size on the wire for a payload of this length.
inline size_t plainFrameSize(size_t payload_length) { return kHeaderSize + 1 + payload_length; }
inline size_t sealedFrameSize(size_t payload_length) {
  return kHeaderSize + 1 + payload_length + kTagSize;
}

// Completes a frame whose payload already sits at frame + kHeaderSize + 1:
// writes the header and type and, with a key, seals type and payload in place
// and appends the tag. Returns the frame size, or 0 when it is too large.
inline size_t finishFrame(const uint8_t* key, uint64_t counter, uint8_t type,
                          size_t payload_length, uint8_t* frame, size_t frame_capacity) {
  const size_t body = 1 + payload_length + (key ? kTagSize : 0);
  if (body > kMaxFrameLength || kHeaderSize + body > frame_capacity) return 0;
  putU32(frame, static_cast<uint32_t>(body));
  frame[kHeaderSize] = type;
  if (key) {
    uint8_t nonce[ht_crypto::kAeadNonceSize];
    counterNonce(counter, nonce);
    uint8_t* plaintext = frame + kHeaderSize;
    ht_crypto::chacha20Poly1305Seal(key, nonce, frame, kHeaderSize, plaintext,
                                    1 + payload_length, plaintext,
                                    plaintext + 1 + payload_length);
  }
  return kHeaderSize + body;
}

// Opens a sealed body in place: afterwards body[0] is the type and the
// payload follows. False when it does not authenticate.
inline bool openFrame(const uint8_t key[kKeySize], uint64_t counter,
                      const uint8_t header[kHeaderSize], uint8_t* body, size_t body_length,
                      size_t* payload_length) {
  if (body_length < 1 + kTagSize) return false;
  uint8_t nonce[ht_crypto::kAeadNonceSize];
  counterNonce(counter, nonce);
  const size_t sealed = body_length - kTagSize;
  if (!ht_crypto::chacha20Poly1305Open(key, nonce, header, kHeaderSize, body, sealed,
                                       body + sealed, body)) {
    return false;
  }
  *payload_length = sealed - 1;
  return true;
}

// Publish payload: flags || u16be(topic length) || topic || data. Writes the
// first part to out; returns its size (3 + topic length) or 0.
inline size_t publishHeader(const char* topic, size_t topic_length, bool retain, uint8_t* out,
                            size_t out_size) {
  if (!validTopic(topic, topic_length) || out_size < 3 + topic_length) return 0;
  out[0] = retain ? kFlagRetain : 0;
  putU16(out + 1, static_cast<uint32_t>(topic_length));
  memcpy(out + 3, topic, topic_length);
  return 3 + topic_length;
}

struct Publish {
  const char* topic = nullptr;  // Not NUL-terminated.
  size_t topic_length = 0;
  const uint8_t* data = nullptr;
  size_t data_length = 0;
  bool retain = false;
};

inline bool parsePublish(const uint8_t* payload, size_t length, Publish& out) {
  if (!payload || length < 3 || (payload[0] & ~kFlagRetain) != 0) return false;
  const size_t topic_length = (static_cast<size_t>(payload[1]) << 8) | payload[2];
  if (topic_length == 0 || topic_length > kMaxTopicLength || length < 3 + topic_length) {
    return false;
  }
  const char* topic = reinterpret_cast<const char*>(payload + 3);
  if (!validTopic(topic, topic_length)) return false;
  out.topic = topic;
  out.topic_length = topic_length;
  out.data = payload + 3 + topic_length;
  out.data_length = length - 3 - topic_length;
  out.retain = (payload[0] & kFlagRetain) != 0;
  return out.data_length <= kMaxPayloadLength;
}

// Stream begin: flags || u16be(topic length) || topic || u32be(total), the
// total 1..kMaxStreamLength. Data frames follow with at most kMaxStreamChunk
// bytes each, then an empty end frame; other frames may come between them.
struct StreamBegin {
  const char* topic = nullptr;  // Not NUL-terminated.
  size_t topic_length = 0;
  size_t total = 0;
  bool retain = false;
};

inline bool parseStreamBegin(const uint8_t* payload, size_t length, StreamBegin& out) {
  if (!payload || length < 3 || (payload[0] & ~kFlagRetain) != 0) return false;
  const size_t topic_length = (static_cast<size_t>(payload[1]) << 8) | payload[2];
  if (topic_length == 0 || topic_length > kMaxTopicLength || length != 3 + topic_length + 4) {
    return false;
  }
  const char* topic = reinterpret_cast<const char*>(payload + 3);
  if (!validTopic(topic, topic_length)) return false;
  const size_t total = getU32(payload + 3 + topic_length);
  if (total == 0 || total > kMaxStreamLength) return false;
  out.topic = topic;
  out.topic_length = topic_length;
  out.total = total;
  out.retain = (payload[0] & kFlagRetain) != 0;
  return true;
}

}  // namespace bridge_link
