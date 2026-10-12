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
//       answered: {"v":1,"state":"idle"|"listening","id":"<id>"} plus
//       "error":"<code>" when the last session failed.

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

enum class PublicState : uint8_t { Idle, Listening };

enum class ErrorCode : uint8_t {
  None,
  MicUnavailable,
  Busy,
  OutOfMemory,
  Timeout,
  Invalid,
};

inline const char* publicStateName(PublicState state) {
  return state == PublicState::Listening ? "listening" : "idle";
}

inline const char* errorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::None: return "";
    case ErrorCode::MicUnavailable: return "mic_unavailable";
    case ErrorCode::Busy: return "busy";
    case ErrorCode::OutOfMemory: return "out_of_memory";
    case ErrorCode::Timeout: return "timeout";
    case ErrorCode::Invalid: return "invalid";
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
};

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
  if (used + 2 > size) return 0;
  out[used++] = '}';
  out[used] = '\0';
  return used;
}

}  // namespace assist_contract
