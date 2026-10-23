#pragma once

// Pure, host-testable part of the Assist microphone feature: the MQTT
// contract shared with the Home Assistant Bridge, the binary audio frame
// header, the retained status payload and the stereo-to-mono conversion used
// by the capture worker. Nothing in this header touches hardware, FreeRTOS or
// Arduino.
//
// MQTT contract v1 (base = the configured device base topic):
//   {base}/cmnd/assist   Bridge -> panel, QoS0, not retained. Sealed on the
//       encrypted command channel (data name "assist") once pairing is active.
//       {"v":1,"op":"start","id":"<16-32 lowercase hex>","max_ms":15000}
//           max_ms clamps to 1000..30000 (default 15000 when absent). A start
//           replaces a running session and stops speaker playback first.
//       {"v":1,"op":"stop","id":"<id>"}
//           Ends the session with that id; other ids are ignored.
//   {base}/stat/assist_audio   panel -> Bridge, QoS0, not retained, binary:
//       "HTA1", id length N (1 byte), id (N bytes), flags (1 byte, bit0 = last
//       frame of the session), sequence (uint32 little-endian, from 0 per
//       session), then signed 16-bit little-endian mono PCM at 16 kHz,
//       normally 100 ms (3200 bytes) per frame; the last frame may be shorter
//       or empty.
//   {base}/stat/assist   panel -> Bridge, QoS0, retained, only once the mic
//       answered: {"v":1,"state":"idle"|"listening"|"wake","id":"<id>"} plus
//       "error":"<code>" when the last session failed, and always
//       "wake_words":[...] (words this firmware can detect) and
//       "active_wake_words":[...] (words it currently listens for).
//
// v1 addendum, on-panel wake word:
//   {base}/cmnd/assist {"v":1,"op":"wake_words","active":["okay_nabu"]}
//       Sets the active wake words (empty list = off), applied at once and
//       stored in NVS for the next boot. Unknown or malformed words are
//       ignored; ids are [a-z0-9_]{1,32}.
//
// v1 addendum, microphone privacy switch (stored in NVS, default on):
//   {base}/cmnd/assist {"v":1,"op":"mic","enabled":false}
//       Turning the microphone off is always accepted; turning it on from
//       MQTT is accepted only on the encrypted command channel, so the
//       panel's own switch cannot be undone by an unauthenticated message.
//       While off, starts are refused with "error":"mic_off" and no wake
//       word runs. The status adds "mic_enabled":true|false.
//   {base}/stat/assist_wake   panel -> Bridge, QoS0, not retained:
//       {"v":1,"word":"okay_nabu","seq":<uint32 per boot>} on a detection.
//       The panel then buffers up to 3 s of audio and sends it first when a
//       start for any id arrives within 3 s; the state is "wake" meanwhile.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/audio/speaker_contract.h"

namespace assist_contract {

constexpr int kProtocolVersion = 1;
constexpr const char* kCommandLeaf = "/cmnd/assist";
constexpr const char* kStatusLeaf = "/stat/assist";
constexpr const char* kAudioLeaf = "/stat/assist_audio";
constexpr const char* kWakeLeaf = "/stat/assist_wake";
constexpr const char* kSealedDataName = "assist";

constexpr size_t kMinRequestIdLength = speaker_contract::kMinRequestIdLength;
constexpr size_t kMaxRequestIdLength = speaker_contract::kMaxRequestIdLength;
constexpr size_t kMaxRequestPayloadBytes = 256;

constexpr uint32_t kMinMaxMs = 1000;
constexpr uint32_t kMaxMaxMs = 30000;
constexpr uint32_t kDefaultMaxMs = 15000;

constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kFrameMs = 100;
constexpr size_t kFrameSamples = kSampleRate * kFrameMs / 1000;
constexpr size_t kFrameBytes = kFrameSamples * sizeof(int16_t);

constexpr char kFrameMagic[4] = {'H', 'T', 'A', '1'};
constexpr uint8_t kFlagLast = 0x01;
// Magic, id length, id, flags, sequence.
constexpr size_t kFixedHeaderBytes = 4 + 1 + 1 + 4;
constexpr size_t kMaxFrameHeaderBytes = kFixedHeaderBytes + kMaxRequestIdLength;

enum class PublicState : uint8_t { Idle, Listening, Wake };

// Wake words the firmware knows, in catalog order. Masks are bit i = entry
// i and live only in RAM; NVS stores the names, so entries may be appended
// or removed without remapping stored settings.
constexpr size_t kMaxWakeWordIdLength = 32;
constexpr const char* kWakeWordIds[] = {"okay_nabu", "hey_jarvis", "alexa"};
constexpr size_t kWakeWordCount = sizeof(kWakeWordIds) / sizeof(kWakeWordIds[0]);
static_assert(kWakeWordCount <= 32, "wake word masks are 32-bit");
constexpr uint32_t kAllWakeWordsMask =
    kWakeWordCount >= 32 ? 0xFFFFFFFFu : ((1u << kWakeWordCount) - 1u);
// After a detection, how long a start may arrive to take the buffered audio.
constexpr uint32_t kWakeHandoffMs = 3000;
// Longest list the stored setting and the status payload need: every catalog
// id plus separators.
constexpr size_t kMaxWakeWordListChars = kWakeWordCount * (kMaxWakeWordIdLength + 3) + 1;

enum class ErrorCode : uint8_t {
  None,
  MicUnavailable,
  Busy,
  OutOfMemory,
  Timeout,
  Invalid,
  MicOff,
};

inline const char* publicStateName(PublicState state) {
  switch (state) {
    case PublicState::Listening: return "listening";
    case PublicState::Wake: return "wake";
    case PublicState::Idle: break;
  }
  return "idle";
}

// [a-z0-9_]{1,32}, the id rule the Bridge also enforces.
inline bool isValidWakeWordId(const char* id, size_t length) {
  if (!id || length == 0 || length > kMaxWakeWordIdLength) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

// Catalog index of a wake word id, or -1 when it is invalid or unknown.
inline int wakeWordIndex(const char* id, size_t length) {
  if (!isValidWakeWordId(id, length)) return -1;
  for (size_t i = 0; i < kWakeWordCount; ++i) {
    if (strlen(kWakeWordIds[i]) == length && memcmp(kWakeWordIds[i], id, length) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Stored setting: comma-separated ids. Unknown and malformed entries are
// dropped, so a word removed from the catalog simply turns off.
inline uint32_t parseWakeWordList(const char* list) {
  uint32_t mask = 0;
  if (!list) return 0;
  const char* start = list;
  for (;;) {
    const char* end = start;
    while (*end && *end != ',') ++end;
    const int index = wakeWordIndex(start, static_cast<size_t>(end - start));
    if (index >= 0) mask |= 1u << index;
    if (!*end) break;
    start = end + 1;
  }
  return mask;
}

// Writes the ids of mask joined by separator, each wrapped in quote (may be
// '\0' for none). Returns the length, or 0 with out = "" when it does not fit.
inline size_t formatWakeWordList(uint32_t mask, char* out, size_t size, char separator = ',',
                                 char quote = '\0') {
  if (!out || size == 0) return 0;
  size_t used = 0;
  bool first = true;
  for (size_t i = 0; i < kWakeWordCount; ++i) {
    if (!(mask & (1u << i))) continue;
    const size_t id_length = strlen(kWakeWordIds[i]);
    const size_t need = (first ? 0 : 1) + id_length + (quote ? 2 : 0);
    if (used + need + 1 > size) {
      out[0] = '\0';
      return 0;
    }
    if (!first) out[used++] = separator;
    if (quote) out[used++] = quote;
    memcpy(out + used, kWakeWordIds[i], id_length);
    used += id_length;
    if (quote) out[used++] = quote;
    first = false;
  }
  out[used] = '\0';
  return used;
}

// Listening and the chosen word are separate settings, so turning listening
// off keeps the word. A new panel does not listen and offers the first
// catalog word.
constexpr uint32_t kDefaultWakeWordMask = 1u;

struct WakeSetting {
  uint32_t chosen = kDefaultWakeWordMask;
  bool listen = false;
};

inline uint32_t activeWakeWordMask(const WakeSetting& setting) {
  return setting.listen ? setting.chosen : 0;
}

// A wake_words command. With "listen" (the Bridge's listen switch) it sets
// listening and, when the list is not empty, the chosen words. Without it
// (Home Assistant's own wake word setting, older Bridges) the list is the
// active set: words are chosen and listened for, an empty list stops
// listening and keeps the word.
inline WakeSetting applyWakeWordsCommand(WakeSetting setting, uint32_t words, bool has_listen,
                                         bool listen) {
  if (words) setting.chosen = words;
  setting.listen = has_listen ? listen : words != 0;
  return setting;
}

// Stored settings: the chosen list and the listen switch. Firmware before
// the switch stored only the active list: a word there was listening, an
// empty list was off.
inline WakeSetting loadWakeSetting(const char* list, bool has_listen, bool listen) {
  return applyWakeWordsCommand(WakeSetting{}, parseWakeWordList(list), has_listen, listen);
}

inline const char* errorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::None: return "";
    case ErrorCode::MicUnavailable: return "mic_unavailable";
    case ErrorCode::Busy: return "busy";
    case ErrorCode::OutOfMemory: return "out_of_memory";
    case ErrorCode::Timeout: return "timeout";
    case ErrorCode::Invalid: return "invalid";
    case ErrorCode::MicOff: return "mic_off";
  }
  return "unknown";
}

inline bool isValidRequestId(const char* id, size_t length) {
  return speaker_contract::isValidRequestId(id, length);
}

// NaN and out-of-range values clamp; the Bridge sends whole milliseconds.
inline uint32_t clampMaxMs(double max_ms) {
  if (!(max_ms == max_ms)) return kDefaultMaxMs;
  if (max_ms <= static_cast<double>(kMinMaxMs)) return kMinMaxMs;
  if (max_ms >= static_cast<double>(kMaxMaxMs)) return kMaxMaxMs;
  return static_cast<uint32_t>(max_ms);
}

inline size_t frameHeaderLength(size_t id_length) { return kFixedHeaderBytes + id_length; }

// Writes the frame header for a valid id. Returns its length, or 0 when the
// id is invalid or the buffer is too small.
inline size_t buildFrameHeader(uint8_t* out, size_t size, const char* id, size_t id_length,
                               bool last, uint32_t sequence) {
  if (!out || !isValidRequestId(id, id_length)) return 0;
  const size_t length = frameHeaderLength(id_length);
  if (size < length) return 0;
  memcpy(out, kFrameMagic, sizeof(kFrameMagic));
  size_t pos = sizeof(kFrameMagic);
  out[pos++] = static_cast<uint8_t>(id_length);
  memcpy(out + pos, id, id_length);
  pos += id_length;
  out[pos++] = last ? kFlagLast : 0;
  for (int i = 0; i < 4; ++i) out[pos++] = static_cast<uint8_t>(sequence >> (8 * i));
  return pos;
}

// Averages interleaved stereo frames (one mic per slot) to mono samples.
inline void stereoToMono(const int16_t* stereo, size_t frames, int16_t* out) {
  for (size_t i = 0; i < frames; ++i) {
    const int32_t sum = static_cast<int32_t>(stereo[2 * i]) + stereo[2 * i + 1];
    out[i] = static_cast<int16_t>(sum / 2);
  }
}

// Takes the left slot of interleaved stereo frames: a single ADC whose data
// is certain only on the left (the ES8311 puts its DAC reference on the
// right when asked to).
inline void stereoLeftToMono(const int16_t* stereo, size_t frames, int16_t* out) {
  for (size_t i = 0; i < frames; ++i) out[i] = stereo[2 * i];
}

// Averages interleaved stereo frames (one mic per slot) to mono and writes
// the samples as signed 16-bit little-endian bytes, 2 bytes per frame.
inline void stereoToMonoLe(const int16_t* stereo, size_t frames, uint8_t* out) {
  for (size_t i = 0; i < frames; ++i) {
    const int32_t sum = static_cast<int32_t>(stereo[2 * i]) + stereo[2 * i + 1];
    const uint16_t mono = static_cast<uint16_t>(static_cast<int16_t>(sum / 2));
    out[2 * i] = static_cast<uint8_t>(mono & 0xFF);
    out[2 * i + 1] = static_cast<uint8_t>(mono >> 8);
  }
}

struct StatusFields {
  PublicState state = PublicState::Idle;
  const char* id = nullptr;
  ErrorCode error = ErrorCode::None;
  // Adds "wake_words" and "active_wake_words" (always, once the mic is ready).
  bool wake_words = false;
  uint32_t available_wake_words = 0;
  uint32_t active_wake_words = 0;
  // Adds "chosen_wake_words" and "wake_listen" (the listen switch) when set.
  bool wake_choice = false;
  uint32_t chosen_wake_words = 0;
  bool wake_listen = false;
  // Adds "mic_enabled" (the privacy switch) when set.
  bool mic_state = false;
  bool mic_enabled = true;
};

// Appends ,"<key>":[...] for the ids in mask. Returns false when it does not
// fit.
inline bool appendWakeWordArray(char* out, size_t size, size_t* used, const char* key,
                                uint32_t mask) {
  char list[kMaxWakeWordListChars + 2 * kWakeWordCount];
  if (formatWakeWordList(mask, list, sizeof(list), ',', '"') == 0 && (mask & kAllWakeWordsMask)) {
    return false;
  }
  const int written = snprintf(out + *used, size - *used, ",\"%s\":[%s]", key, list);
  if (written < 0 || static_cast<size_t>(written) >= size - *used) return false;
  *used += static_cast<size_t>(written);
  return true;
}

// Returns the JSON length, or 0 when the buffer is too small.
inline size_t buildStatusJson(char* out, size_t size, const StatusFields& fields) {
  if (!out || size == 0) return 0;
  int written = snprintf(out, size, "{\"v\":%d,\"state\":\"%s\"", kProtocolVersion,
                         publicStateName(fields.state));
  if (written < 0 || static_cast<size_t>(written) >= size) return 0;
  size_t used = static_cast<size_t>(written);
  if (fields.id && isValidRequestId(fields.id, strlen(fields.id))) {
    written = snprintf(out + used, size - used, ",\"id\":\"%s\"", fields.id);
    if (written < 0 || static_cast<size_t>(written) >= size - used) return 0;
    used += static_cast<size_t>(written);
  }
  if (fields.error != ErrorCode::None) {
    written = snprintf(out + used, size - used, ",\"error\":\"%s\"", errorCodeName(fields.error));
    if (written < 0 || static_cast<size_t>(written) >= size - used) return 0;
    used += static_cast<size_t>(written);
  }
  if (fields.wake_words) {
    if (!appendWakeWordArray(out, size, &used, "wake_words", fields.available_wake_words) ||
        !appendWakeWordArray(out, size, &used, "active_wake_words",
                             fields.active_wake_words & fields.available_wake_words)) {
      return 0;
    }
  }
  if (fields.wake_choice) {
    if (!appendWakeWordArray(out, size, &used, "chosen_wake_words",
                             fields.chosen_wake_words & fields.available_wake_words)) {
      return 0;
    }
    written = snprintf(out + used, size - used, ",\"wake_listen\":%s",
                       fields.wake_listen ? "true" : "false");
    if (written < 0 || static_cast<size_t>(written) >= size - used) return 0;
    used += static_cast<size_t>(written);
  }
  if (fields.mic_state) {
    written = snprintf(out + used, size - used, ",\"mic_enabled\":%s",
                       fields.mic_enabled ? "true" : "false");
    if (written < 0 || static_cast<size_t>(written) >= size - used) return 0;
    used += static_cast<size_t>(written);
  }
  if (used + 2 > size) return 0;
  out[used++] = '}';
  out[used] = '\0';
  return used;
}

// {base}/stat/assist_wake payload. Returns the JSON length, or 0 when the
// word is invalid or the buffer is too small.
inline size_t buildWakeJson(char* out, size_t size, const char* word, uint32_t sequence) {
  if (!out || size == 0 || !word || !isValidWakeWordId(word, strlen(word))) return 0;
  const int written = snprintf(out, size, "{\"v\":%d,\"word\":\"%s\",\"seq\":%lu}",
                               kProtocolVersion, word, static_cast<unsigned long>(sequence));
  if (written < 0 || static_cast<size_t>(written) >= size) return 0;
  return static_cast<size_t>(written);
}

}  // namespace assist_contract
