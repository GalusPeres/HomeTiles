#pragma once

// Pure, host-testable part of the speaker feature: the MQTT contract shared
// with the Home Assistant Bridge (media_player entity), the retained status
// payload, URL validation, volume mapping and the clip format checks used by
// the playback worker. Nothing in this header touches hardware, FreeRTOS or
// Arduino.
//
// MQTT contract (base = the configured device base topic):
//   {base}/cmnd/audio   Bridge -> panel, QoS0, not retained. Sealed on the
//       encrypted command channel (data name "audio") once pairing is active.
//       {"v":1,"op":"play","id":"<16-32 lowercase hex>","url":"http://..."}
//       "url" may also be "tcp://<host>:<port>/<token>": a clip the Bridge
//       decodes and sends as PCM (audio_stream_contract.h).
//       {"v":1,"op":"stop"}
//       {"v":1,"op":"pause"}  {"v":1,"op":"resume"}
//       {"v":1,"op":"volume","volume":0.5}        0.0 .. 1.0
//       {"v":1,"op":"mute","muted":true}
//   {base}/stat/audio   panel -> Bridge, QoS0, retained,
//       {"v":1,"state":"idle"|"playing"|"paused","volume":0.5,"muted":false,
//        "formats":["mp3","wav"],"max_bytes":4194304,"pausable":true,
//        "stream":"tcp-ack-v1"}
//       plus "id":"<id>" of the current or last clip and "error":"<code>"
//       when that clip failed (cleared by the next play). While a clip
//       plays or pauses: "position_ms" at the moment of the status, and
//       "duration_ms" once the clip's length is known (not for live
//       streams). The status is not repeated while a clip plays; the Bridge
//       moves the position on itself.
//
// A Bridge that sees "stream" decodes the clip itself (any format) and sends
// PCM at the pace the panel acknowledges. Otherwise the panel plays an
// http(s) URL while it downloads, so clips of any length and endless MP3
// streams (internet radio) work. Formats: MP3 (MPEG layer III) and 16-bit
// PCM WAV; AAC, Ogg and FLAC streams are refused.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/audio/audio_stream_contract.h"

namespace speaker_contract {

constexpr int kProtocolVersion = 1;
constexpr const char* kCommandLeaf = "/cmnd/audio";
constexpr const char* kStatusLeaf = "/stat/audio";
constexpr const char* kSealedDataName = "audio";

constexpr size_t kMinRequestIdLength = 16;
constexpr size_t kMaxRequestIdLength = 32;
constexpr size_t kMaxUrlLength = 1024;
constexpr size_t kMaxRequestPayloadBytes = 1536;
// Announced as "max_bytes" for older Bridges. Clips and live streams are
// played while they download, so their length is not limited.
constexpr uint32_t kMaxClipBytes = 4u * 1024u * 1024u;
// Compressed audio between the network and the decoder: 16 s at 128 kbit/s.
constexpr size_t kStreamBufferBytes = 256u * 1024u;
// About 1.5 s of 128 kbit/s MP3 before playback starts or resumes.
constexpr size_t kPrebufferBytes = 24u * 1024u;
// A low bitrate stream starts after this wait with what it has.
constexpr uint32_t kPrebufferMaxWaitMs = 1500;
// More than the largest MP3 frame (1441 bytes), so a frame is never cut.
constexpr size_t kMinDecodeBytes = 2048;
// Metadata chunks in a WAV header before the samples.
constexpr size_t kMaxWavHeaderBytes = 64u * 1024u;
constexpr float kDefaultVolume = 0.5f;
// Volume at 1.0 is 0 dB digital gain; 0.0 mutes. In between the gain falls
// linearly in dB down to -kVolumeRangeDb, which sounds even to the ear.
constexpr float kVolumeRangeDb = 50.0f;
constexpr uint8_t kVolumeRegisterZeroDb = 0xBF;

enum class PublicState : uint8_t { Idle, Playing, Paused };

enum class ErrorCode : uint8_t {
  None,
  InvalidUrl,
  HttpsUnsupported,
  DownloadFailed,
  TooLarge,
  UnsupportedFormat,
  DecodeFailed,
  CodecUnavailable,
  OutOfMemory,
};

inline const char* publicStateName(PublicState state) {
  switch (state) {
    case PublicState::Playing: return "playing";
    case PublicState::Paused: return "paused";
    case PublicState::Idle: break;
  }
  return "idle";
}

inline const char* errorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::None: return "";
    case ErrorCode::InvalidUrl: return "invalid_url";
    case ErrorCode::HttpsUnsupported: return "https_unsupported";
    case ErrorCode::DownloadFailed: return "download_failed";
    case ErrorCode::TooLarge: return "too_large";
    case ErrorCode::UnsupportedFormat: return "unsupported_format";
    case ErrorCode::DecodeFailed: return "decode_failed";
    case ErrorCode::CodecUnavailable: return "codec_unavailable";
    case ErrorCode::OutOfMemory: return "out_of_memory";
  }
  return "unknown";
}

inline bool isValidRequestId(const char* id, size_t length) {
  if (!id || length < kMinRequestIdLength || length > kMaxRequestIdLength) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// NaN and out-of-range values clamp; the Bridge sends 0.0 .. 1.0.
inline float clampVolume(double volume) {
  if (!(volume == volume)) return kDefaultVolume;
  if (volume <= 0.0) return 0.0f;
  if (volume >= 1.0) return 1.0f;
  return static_cast<float>(volume);
}

// ES8311 DAC volume register 0x32 (0.5 dB steps, 0xBF = 0 dB, 0 = mute).
inline uint8_t volumeRegister(float volume) {
  const float clamped = clampVolume(volume);
  if (clamped <= 0.0f) return 0;
  const float db = -kVolumeRangeDb * (1.0f - clamped);
  const long steps = lroundf(db * 2.0f);
  const long value = static_cast<long>(kVolumeRegisterZeroDb) + steps;
  return static_cast<uint8_t>(value < 1 ? 1 : value);
}

// The register for a board whose amplifier or supply cannot take the full
// scale: its loudest clean level is `limit` (0..1) of the shared scale, so
// 100 % plays like `limit` and every step below scales with it. The volume
// Home Assistant shows stays the user's.
inline uint8_t limitedVolumeRegister(float volume, float limit) {
  if (!(limit > 0.0f) || limit > 1.0f) limit = 1.0f;
  return volumeRegister(clampVolume(volume) * limit);
}

// Absolute http:// or https:// URL with a host, printable ASCII without
// spaces, or the Bridge's tcp:// stream URL. A caller may refuse https://
// (https_allowed = false).
inline ErrorCode validateUrl(const char* url, size_t length, bool https_allowed) {
  if (!url || length == 0 || length > kMaxUrlLength) return ErrorCode::InvalidUrl;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(url[i]);
    if (c <= 0x20 || c >= 0x7F) return ErrorCode::InvalidUrl;
  }
  if (length > 6 && strncmp(url, "tcp://", 6) == 0) {
    audio_stream::Endpoint endpoint;
    return audio_stream::parseStreamUrl(url, length, &endpoint) ? ErrorCode::None
                                                                : ErrorCode::InvalidUrl;
  }
  size_t host = 0;
  if (length > 7 && strncmp(url, "http://", 7) == 0) {
    host = 7;
  } else if (length > 8 && strncmp(url, "https://", 8) == 0) {
    if (!https_allowed) return ErrorCode::HttpsUnsupported;
    host = 8;
  } else {
    return ErrorCode::InvalidUrl;
  }
  if (url[host] == '/' || url[host] == '?' || url[host] == '#') return ErrorCode::InvalidUrl;
  return ErrorCode::None;
}

enum class ClipFormat : uint8_t { Unknown, Mp3, Wav };

inline uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// By content, never by URL or Content-Type: TTS proxies often send neither.
inline ClipFormat detectFormat(const uint8_t* data, size_t length) {
  if (!data) return ClipFormat::Unknown;
  if (length >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0) {
    return ClipFormat::Wav;
  }
  if (length >= 3 && memcmp(data, "ID3", 3) == 0) return ClipFormat::Mp3;
  // MPEG audio frame sync (11 set bits), layer III.
  if (length >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0 && (data[1] & 0x06) == 0x02) {
    return ClipFormat::Mp3;
  }
  return ClipFormat::Unknown;
}

struct WavInfo {
  uint32_t sample_rate = 0;
  uint16_t channels = 0;
  // Bytes before the first sample.
  size_t data_offset = 0;
  // Declared data chunk size; 0 or 0xFFFFFFFF (live streams) means "until
  // the connection ends".
  uint32_t data_length = 0;
};

enum class WavHeaderResult : uint8_t { Incomplete, Ok, Invalid };

// Parses the RIFF header from the first bytes of a download. Incomplete
// means more bytes are needed to reach the data chunk. Accepts 16-bit PCM
// (format 1, or WAVE_FORMAT_EXTENSIBLE with the PCM sub-format), mono or
// stereo.
inline WavHeaderResult parseWavHeader(const uint8_t* data, size_t length, WavInfo* out) {
  if (!out || !data) return WavHeaderResult::Invalid;
  // Compare what arrived of "RIFF" ... "WAVE" so a non-WAV fails early.
  static const char kRiff[] = "RIFF????WAVE";
  for (size_t i = 0; i < 12 && i < length; ++i) {
    if (kRiff[i] != '?' && data[i] != static_cast<uint8_t>(kRiff[i])) {
      return WavHeaderResult::Invalid;
    }
  }
  if (length < 12) return WavHeaderResult::Incomplete;
  *out = WavInfo{};
  bool have_fmt = false;
  size_t pos = 12;
  for (;;) {
    if (pos + 8 > length) return WavHeaderResult::Incomplete;
    const uint8_t* chunk = data + pos;
    const uint32_t size = readLe32(chunk + 4);
    const size_t body = pos + 8;
    if (memcmp(chunk, "data", 4) == 0) {
      if (!have_fmt) return WavHeaderResult::Invalid;
      out->data_offset = body;
      out->data_length = size;
      return WavHeaderResult::Ok;
    }
    if (memcmp(chunk, "fmt ", 4) == 0) {
      if (size < 16) return WavHeaderResult::Invalid;
      if (body + 16 > length) return WavHeaderResult::Incomplete;
      uint16_t format = readLe16(data + body);
      if (format == 0xFFFE) {
        if (size < 40) return WavHeaderResult::Invalid;
        if (body + 26 > length) return WavHeaderResult::Incomplete;
        format = readLe16(data + body + 24);
      }
      const uint16_t channels = readLe16(data + body + 2);
      const uint32_t rate = readLe32(data + body + 4);
      const uint16_t bits = readLe16(data + body + 14);
      if (format != 1 || bits != 16 || channels < 1 || channels > 2 || rate == 0) {
        return WavHeaderResult::Invalid;
      }
      out->channels = channels;
      out->sample_rate = rate;
      have_fmt = true;
    }
    // Chunks are padded to an even size. Metadata chunks before "data" are
    // small; a huge one is a broken header, not something to wait for.
    const uint64_t next = static_cast<uint64_t>(body) + size + (size & 1u);
    if (next > kMaxWavHeaderBytes) return WavHeaderResult::Invalid;
    pos = static_cast<size_t>(next);
  }
}

// Content types the MP3 decoder cannot play. Many servers send no type or a
// generic one, so anything else is decided by the bytes.
inline bool isUnsupportedContentType(const char* type) {
  if (!type) return false;
  static const char* const kRejected[] = {"aac", "mp4", "m4a", "ogg", "opus",
                                          "flac", "vorbis", "mpegurl", "x-scpls", "text/"};
  char lower[64];
  size_t n = 0;
  for (; type[n] && n + 1 < sizeof(lower); ++n) {
    const char c = type[n];
    lower[n] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
  lower[n] = '\0';
  for (const char* rejected : kRejected) {
    if (strstr(lower, rejected)) return true;
  }
  return false;
}

// A frame header the firmware's Helix copy (src/audio/helix_mp3) can decode:
// MPEG-1, 2 or 2.5 (11-bit sync word, version not reserved), Layer III, a
// bitrate from the table and a valid sample rate. Needs 4 bytes. Helix takes
// any sync word for a frame: a Layer I/II or free-format header found in tag
// data put it into free-bitrate mode for good, and the next frames were
// copied with a length of -1 over its own memory (#119: an MPEG-2.5 clip at
// 8-12 kHz restarted the panel with the core's copy, which cannot see those
// frames at all).
inline bool isDecodableMp3Header(const uint8_t* header) {
  if (!header || header[0] != 0xFF || (header[1] & 0xE0) != 0xE0) return false;
  if (((header[1] >> 3) & 0x03) == 0x01) return false;  // reserved MPEG version
  if (((header[1] >> 1) & 0x03) != 0x01) return false;  // Layer III
  const uint8_t bitrate = header[2] >> 4;
  if (bitrate == 0 || bitrate == 15) return false;      // free format, invalid
  return ((header[2] >> 2) & 0x03) != 3;                // reserved sample rate
}

// Samples per channel in one Layer III frame: 1152 for MPEG-1, 576 for
// MPEG-2 and 2.5.
inline uint32_t mp3SamplesPerFrame(const uint8_t* header) {
  return ((header[1] >> 3) & 0x03) == 0x03 ? 1152u : 576u;
}

inline uint32_t readBigEndian32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// Frame count of the clip from the Xing/Info (LAME) or VBRI header in its
// first frame, 0 when the frame carries none. frame points at the frame
// header, length is what is available from there.
inline uint32_t mp3TagFrameCount(const uint8_t* frame, size_t length) {
  if (!frame || length < 4) return 0;
  const bool mpeg1 = ((frame[1] >> 3) & 0x03) == 0x03;
  const bool mono = (frame[3] >> 6) == 0x03;
  // The tag follows the side information.
  const size_t xing = 4 + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
  if (length >= xing + 12 &&
      (memcmp(frame + xing, "Xing", 4) == 0 || memcmp(frame + xing, "Info", 4) == 0) &&
      (readBigEndian32(frame + xing + 4) & 0x01)) {
    return readBigEndian32(frame + xing + 8);
  }
  // VBRI (Fraunhofer) sits 32 bytes after the header in every version.
  const size_t vbri = 4 + 32;
  if (length >= vbri + 18 && memcmp(frame + vbri, "VBRI", 4) == 0) {
    return readBigEndian32(frame + vbri + 14);
  }
  return 0;
}

// Length of an MP3 clip in ms: from the tag's frame count when the first
// frame has one, else from the audio bytes after the ID3 tag and the
// bitrate (constant bitrate). 0 when unknown: a live stream announces no
// size (audio_bytes < 0).
inline uint32_t mp3DurationMs(const uint8_t* first_frame, size_t length, uint32_t sample_rate,
                              uint32_t bitrate_bps, int64_t audio_bytes) {
  if (sample_rate == 0) return 0;
  const uint32_t frames = mp3TagFrameCount(first_frame, length);
  if (frames > 0) {
    return static_cast<uint32_t>(static_cast<uint64_t>(frames) *
                                 mp3SamplesPerFrame(first_frame) * 1000u / sample_rate);
  }
  if (audio_bytes <= 0 || bitrate_bps == 0) return 0;
  return static_cast<uint32_t>(static_cast<uint64_t>(audio_bytes) * 8000u / bitrate_bps);
}

// Length of a WAV clip in ms, 0 when its data size is open (a stream).
inline uint32_t wavDurationMs(uint32_t data_length, uint32_t sample_rate, uint32_t frame_bytes) {
  if (data_length == 0 || data_length == 0xFFFFFFFFu || sample_rate == 0 || frame_bytes == 0) {
    return 0;
  }
  return static_cast<uint32_t>(static_cast<uint64_t>(data_length) * 1000u /
                               (static_cast<uint64_t>(sample_rate) * frame_bytes));
}

// Size of an ID3v2 tag at the start of an MP3 (header, body and footer),
// 0 when there is none. Needs the first 10 bytes.
inline size_t id3v2Length(const uint8_t* data, size_t length) {
  if (!data || length < 10 || memcmp(data, "ID3", 3) != 0) return 0;
  const size_t size = (static_cast<size_t>(data[6] & 0x7F) << 21) |
                      (static_cast<size_t>(data[7] & 0x7F) << 14) |
                      (static_cast<size_t>(data[8] & 0x7F) << 7) |
                      static_cast<size_t>(data[9] & 0x7F);
  return 10 + size + ((data[5] & 0x10) ? 10 : 0);
}

// Playback starts (and restarts after the network fell behind) once enough
// is buffered to ride out a slow moment, or once a slow stream had its time.
inline bool prebufferDone(size_t buffered, uint32_t waited_ms, bool ended) {
  if (ended) return true;
  if (buffered >= kPrebufferBytes) return true;
  return waited_ms >= kPrebufferMaxWaitMs && buffered >= kMinDecodeBytes;
}

// Linear byte buffer between the network and the decoder. The decoder needs
// whole frames in one piece, so instead of wrapping, the unread tail moves to
// the front once the read position passed half the buffer.
struct StreamBuffer {
  uint8_t* data = nullptr;
  size_t capacity = 0;
  size_t start = 0;
  size_t end = 0;

  void reset(uint8_t* memory, size_t size) {
    data = memory;
    capacity = size;
    start = 0;
    end = 0;
  }
  size_t buffered() const { return end - start; }
  const uint8_t* read() const { return data + start; }
  void consume(size_t bytes) {
    start += bytes < buffered() ? bytes : buffered();
    if (start == end) start = end = 0;
  }
  // Free space at the end, after compacting when that helps.
  size_t writable() {
    if (start > 0 && (start >= capacity / 2 || end == capacity)) {
      memmove(data, data + start, end - start);
      end -= start;
      start = 0;
    }
    return capacity - end;
  }
  uint8_t* write() { return data + end; }
  void commit(size_t bytes) {
    const size_t room = capacity - end;
    end += bytes < room ? bytes : room;
  }
};

// In place: interleaved stereo frames to mono (the speaker has one channel).
inline void downmixToMono(int16_t* samples, size_t frames) {
  for (size_t i = 0; i < frames; ++i) {
    const int32_t sum = static_cast<int32_t>(samples[2 * i]) + samples[2 * i + 1];
    samples[i] = static_cast<int16_t>(sum / 2);
  }
}

struct StatusFields {
  PublicState state = PublicState::Idle;
  float volume = kDefaultVolume;
  bool muted = false;
  const char* id = nullptr;
  ErrorCode error = ErrorCode::None;
  // Sent while a clip plays or pauses; duration_ms 0 = not known.
  uint32_t position_ms = 0;
  uint32_t duration_ms = 0;
  // The panel takes clips the Bridge decodes (audio_stream_contract.h).
  bool bridge_stream = false;
};

// Returns the JSON length, or 0 when the buffer is too small.
inline size_t buildStatusJson(char* out, size_t size, const StatusFields& fields) {
  if (!out || size == 0) return 0;
  const float volume = clampVolume(fields.volume);
  int written = snprintf(out, size,
                         "{\"v\":%d,\"state\":\"%s\",\"volume\":%.2f,\"muted\":%s,"
                         "\"formats\":[\"mp3\",\"wav\"],\"max_bytes\":%lu,\"pausable\":true",
                         kProtocolVersion, publicStateName(fields.state),
                         static_cast<double>(volume), fields.muted ? "true" : "false",
                         static_cast<unsigned long>(kMaxClipBytes));
  if (written < 0 || static_cast<size_t>(written) >= size) return 0;
  size_t used = static_cast<size_t>(written);
  if (fields.bridge_stream) {
    written = snprintf(out + used, size - used, ",\"stream\":\"%s\"", audio_stream::kTransport);
    if (written < 0 || static_cast<size_t>(written) >= size - used) return 0;
    used += static_cast<size_t>(written);
  }
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
  if (fields.state != PublicState::Idle) {
    written = snprintf(out + used, size - used, ",\"position_ms\":%lu",
                       static_cast<unsigned long>(fields.position_ms));
    if (written < 0 || static_cast<size_t>(written) >= size - used) return 0;
    used += static_cast<size_t>(written);
    if (fields.duration_ms > 0) {
      written = snprintf(out + used, size - used, ",\"duration_ms\":%lu",
                         static_cast<unsigned long>(fields.duration_ms));
      if (written < 0 || static_cast<size_t>(written) >= size - used) return 0;
      used += static_cast<size_t>(written);
    }
  }
  if (used + 2 > size) return 0;
  out[used++] = '}';
  out[used] = '\0';
  return used;
}

}  // namespace speaker_contract
