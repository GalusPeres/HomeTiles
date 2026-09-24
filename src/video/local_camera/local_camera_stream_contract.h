#pragma once

// Pure, host-testable part of the built-in camera live stream (panel ->
// Bridge upload). Nothing here touches hardware, sockets, FreeRTOS or Arduino
// and nothing names a device or a sensor: sizes come from the board's
// SensorMode at runtime.
//
// MQTT, Bridge -> panel on {base}/cmnd/local_camera (QoS 0, never retained):
//   start/keepalive every 2 s:
//     {"v":1,"action":"stream","session":"<16-32 lowercase hex>",
//      "host":"<Bridge IPv4>","port":<1-65535>,"token":"<16-64 lowercase hex>",
//      "width":640,"height":360,"fps":15,"quality":65,"ttl_ms":6000}
//   stop:
//     {"v":1,"action":"stream_stop","session":"<same session>"}
//   Snapshot requests keep their shape without an "action" key.
// width/height/fps/quality are hints; only the Auto mode uses them and a
// deviating hint never rejects a request.
//
// TCP, panel -> host:port (big-endian, structs in src/video/tcp_ack_wire.h):
//   panel  "HTCAMUP/1 <session> <token>\n"         (<= 256 bytes)
//   Bridge hello "HTC1" status u16 (0 ok) chunk u16 (8192)
//   panel  frame header "HTF1" type(1 JPEG, 2 flush, 3 end) 0 0 0 seq len
//   panel  payload in min(chunk, remaining) byte chunks; after each chunk
//   Bridge ack "HTA1" seq cumulative-bytes, before the next chunk is sent.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/video/local_camera/local_camera_contract.h"
#include "src/video/tcp_ack_wire.h"

namespace local_camera_stream {

// ---------------------------------------------------------------------------
// Stream modes. Stable u8 ids persisted in NVS ("lcam_mode"): append only,
// never renumber. 0 = Auto, which follows the Bridge request hints.
// ---------------------------------------------------------------------------
constexpr uint8_t kModeAuto = 0;

struct ModeEntry {
  uint8_t id;
  uint8_t fps;
  uint8_t quality;  // Hardware JPEG quality at the start of a stream.
};

// Every mode streams the full board image (whatever size the board delivers:
// 1280x720 on the V2, 960x544 on the Waveshare 8-inch); a mode is only a rate
// and a start quality. Higher rates use a lower quality to keep the frames
// small. Adding a mode is one line here.
constexpr ModeEntry kModes[] = {
    {1, 5, 65},
    {2, 10, 45},
    {3, 15, 50},
    {4, 20, 45},
    {5, 25, 40},
};
constexpr size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

constexpr uint8_t kMinQuality = 30;
// The Custom mode slider reaches the floor of oversized frames, so a noisy
// night image can be kept small on purpose.
constexpr uint8_t kCustomMinQuality = 10;
constexpr uint8_t kMaxQuality = 90;
constexpr uint8_t kQualityStep = 5;
constexpr uint8_t kMaxFps = 30;

inline const ModeEntry* findMode(uint8_t id) {
  for (const ModeEntry& mode : kModes) {
    if (mode.id == id) return &mode;
  }
  return nullptr;
}

// User-defined rate and quality (Web Admin "Custom"), stored in their own NVS
// keys. The id lies outside the table range so appended modes never collide.
constexpr uint8_t kModeCustom = 100;
constexpr uint8_t kCustomMinFps = 1;
constexpr uint8_t kCustomMaxFps = 25;

struct CustomMode {
  uint8_t fps = 1;
  uint8_t quality = 65;
};

inline CustomMode makeCustomMode(long fps, long quality) {
  CustomMode mode;
  mode.fps = static_cast<uint8_t>(fps < kCustomMinFps   ? kCustomMinFps
                                  : fps > kCustomMaxFps ? kCustomMaxFps
                                                        : fps);
  mode.quality = static_cast<uint8_t>(quality < kCustomMinQuality ? kCustomMinQuality
                                      : quality > kMaxQuality     ? kMaxQuality
                                                                  : quality);
  return mode;
}

inline bool isKnownMode(uint8_t id) {
  return id == kModeAuto || id == kModeCustom || findMode(id) != nullptr;
}

// Untranslated technical label with the board image size, e.g.
// "1280x720, 5 fps, q65".
inline size_t formatModeLabel(char* out, size_t capacity, const ModeEntry& mode,
                              uint16_t image_width, uint16_t image_height) {
  if (!out || capacity == 0) return 0;
  const int written = snprintf(out, capacity, "%ux%u, %u fps, q%u",
                               static_cast<unsigned>(image_width),
                               static_cast<unsigned>(image_height),
                               static_cast<unsigned>(mode.fps),
                               static_cast<unsigned>(mode.quality));
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

// Request hints; the defaults are the Bridge's current request.
struct StreamHints {
  uint16_t width = 640;
  uint16_t height = 360;
  uint8_t fps = 15;
  uint8_t quality = 65;
};

inline bool sameHints(const StreamHints& a, const StreamHints& b) {
  return a.width == b.width && a.height == b.height && a.fps == b.fps &&
         a.quality == b.quality;
}

// What the capture loop really produces.
struct StreamSettings {
  uint8_t mode_id = kModeAuto;  // The selected setting (0 = Auto).
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t fps = 0;
  uint8_t quality = 0;
  bool half = false;            // 2x2 averaging of the full image.
};

inline uint8_t clampQuality(long value) {
  if (value < kMinQuality) return kMinQuality;
  if (value > kMaxQuality) return kMaxQuality;
  return static_cast<uint8_t>(value);
}

// The hardware JPEG encoder needs whole 16x8 MCUs (YUV422).
inline bool outputSizeUsable(uint32_t width, uint32_t height) {
  return width >= 16 && height >= 8 && width % 16 == 0 && height % 8 == 0;
}

// True when (width, height) is the full image or its exact half on this board.
inline bool sizeSupported(uint16_t width, uint16_t height, uint16_t image_width,
                          uint16_t image_height, bool* half) {
  if (width == image_width && height == image_height &&
      outputSizeUsable(width, height)) {
    if (half) *half = false;
    return true;
  }
  if (image_width % 2 == 0 && image_height % 2 == 0 &&
      width == image_width / 2 && height == image_height / 2 &&
      outputSizeUsable(width, height)) {
    if (half) *half = true;
    return true;
  }
  return false;
}

// Highest table fps; the modes apply to every board image size (b26 keyed
// them to 1280x720 and capped the 960x544 image at 5 fps).
inline uint8_t maxTableFps() {
  uint8_t best = 0;
  for (const ModeEntry& mode : kModes) {
    if (mode.fps > best) best = mode.fps;
  }
  return best;
}

// Resolves the stored setting (or Auto) against the request hints, the
// user-defined values (Custom) and the board image size. Returns false only
// when the board supports no size.
inline bool resolveSettings(uint8_t mode_id, const StreamHints& hints,
                            uint16_t image_width, uint16_t image_height,
                            StreamSettings* out, const CustomMode& custom = CustomMode{}) {
  if (!out) return false;
  StreamSettings settings;
  settings.mode_id = isKnownMode(mode_id) ? mode_id : kModeAuto;
  if (settings.mode_id == kModeCustom) {
    // The full image like every mode; the rate stops at the fastest table
    // mode for this size.
    if (!outputSizeUsable(image_width, image_height)) return false;
    const CustomMode clamped = makeCustomMode(custom.fps, custom.quality);
    const uint8_t max_fps = maxTableFps();
    settings.width = image_width;
    settings.height = image_height;
    settings.half = false;
    settings.fps = clamped.fps > max_fps ? max_fps : clamped.fps;
    settings.quality = clamped.quality;
    *out = settings;
    return true;
  }
  // Every mode, Auto included, streams the full board image; the hinted size
  // only reaches the Bridge, which scales for its viewers.
  if (!outputSizeUsable(image_width, image_height)) return false;
  settings.width = image_width;
  settings.height = image_height;
  settings.half = false;
  if (const ModeEntry* mode = findMode(settings.mode_id)) {
    settings.fps = mode->fps;
    settings.quality = mode->quality;
    *out = settings;
    return true;
  }
  const uint8_t max_fps = maxTableFps();
  uint8_t fps = hints.fps == 0 ? 1 : hints.fps;
  if (fps > max_fps) fps = max_fps;
  settings.fps = fps;
  settings.quality = clampQuality(hints.quality);
  // Never above the table quality of the slowest mode that reaches this rate.
  const ModeEntry* rate_mode = nullptr;
  for (const ModeEntry& mode : kModes) {
    if (mode.fps < fps) continue;
    if (!rate_mode || mode.fps < rate_mode->fps) rate_mode = &mode;
  }
  if (rate_mode && settings.quality > rate_mode->quality) settings.quality = rate_mode->quality;
  *out = settings;
  return true;
}

// Lower quality after a busy upload, never below kMinQuality.
inline uint8_t reducedQuality(uint8_t quality) {
  return quality > kMinQuality + kQualityStep ? static_cast<uint8_t>(quality - kQualityStep)
                                              : kMinQuality;
}

// Floor for frames over kMaxFrameBytes. Noisy low-light frames can exceed the
// limit even at kMinQuality; a coarse frame beats sending none at all.
constexpr uint8_t kMinOversizeQuality = 10;
static_assert(kCustomMinQuality >= kMinOversizeQuality, "Custom quality below the floor");

// Lower quality after an oversized frame, never below kMinOversizeQuality.
inline uint8_t reducedQualityForSize(uint8_t quality) {
  return quality > kMinOversizeQuality + kQualityStep
             ? static_cast<uint8_t>(quality - kQualityStep)
             : kMinOversizeQuality;
}

// Longest exposure that still fits the mode's frame interval. Exposures above
// default_lines stretch the sensor frame proportionally (frame_ms at default).
inline uint16_t maxExposureLinesForFps(uint8_t fps, uint16_t frame_ms,
                                       uint16_t default_lines, uint16_t max_lines) {
  if (fps == 0 || frame_ms == 0 || default_lines == 0) return max_lines;
  const uint32_t period_ms = 1000u / fps;
  uint32_t lines = static_cast<uint32_t>(default_lines) * period_ms / frame_ms;
  if (lines < default_lines) lines = default_lines;
  if (lines > max_lines) lines = max_lines;
  return static_cast<uint16_t>(lines);
}

// ---------------------------------------------------------------------------
// MQTT stream commands
// ---------------------------------------------------------------------------
constexpr const char* kActionStream = "stream";
constexpr const char* kActionStreamStop = "stream_stop";
// Privacy pause and resume: {"v":1,"action":"pause"} / {"v":1,"action":"resume"}.
constexpr const char* kActionPause = "pause";
constexpr const char* kActionResume = "resume";
constexpr size_t kMinSessionLength = 16;
constexpr size_t kMaxSessionLength = 32;
constexpr size_t kMinTokenLength = 16;
constexpr size_t kMaxTokenLength = 64;
constexpr uint32_t kDefaultTtlMs = 6000;
constexpr uint32_t kMinTtlMs = 1000;
constexpr uint32_t kMaxTtlMs = 60000;

inline bool isLowerHex(const char* text, size_t min_length, size_t max_length) {
  if (!text) return false;
  const size_t length = strnlen(text, max_length + 1);
  if (length < min_length || length > max_length) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = text[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

inline bool isValidSession(const char* session) {
  return isLowerHex(session, kMinSessionLength, kMaxSessionLength);
}

inline bool isValidToken(const char* token) {
  return isLowerHex(token, kMinTokenLength, kMaxTokenLength);
}

// Strict dotted-quad IPv4 (no leading zeros), never 0.0.0.0 or multicast.
inline bool isValidIpv4(const char* text) {
  if (!text) return false;
  const size_t length = strnlen(text, 16);
  if (length < 7 || length > 15) return false;
  unsigned octets[4] = {};
  size_t part = 0;
  size_t digits = 0;
  unsigned value = 0;
  for (size_t i = 0; i <= length; ++i) {
    const char c = i < length ? text[i] : '.';
    if (c >= '0' && c <= '9') {
      if (digits == 1 && value == 0) return false;  // Leading zero.
      value = value * 10u + static_cast<unsigned>(c - '0');
      if (++digits > 3 || value > 255) return false;
      continue;
    }
    if (c != '.' || digits == 0 || part >= 4) return false;
    octets[part++] = value;
    digits = 0;
    value = 0;
  }
  if (part != 4) return false;
  const bool unspecified = octets[0] == 0 && octets[1] == 0 && octets[2] == 0 && octets[3] == 0;
  const bool multicast = octets[0] >= 224 && octets[0] <= 239;
  return !unspecified && !multicast;
}

struct StreamCommand {
  char session[kMaxSessionLength + 1] = {};
  char token[kMaxTokenLength + 1] = {};
  char host[16] = {};
  uint16_t port = 0;
  uint32_t ttl_ms = kDefaultTtlMs;
  StreamHints hints;
};

enum class CommandStatus : uint8_t {
  Ok,
  UnsupportedVersion,
  UnsupportedAction,
  InvalidSession,
  InvalidEndpoint,
  InvalidToken,
  InvalidTtl,
};

inline const char* commandStatusName(CommandStatus status) {
  switch (status) {
    case CommandStatus::Ok: return "ok";
    case CommandStatus::UnsupportedVersion: return "unsupported_version";
    case CommandStatus::UnsupportedAction: return "unsupported_action";
    case CommandStatus::InvalidSession: return "invalid_session";
    case CommandStatus::InvalidEndpoint: return "invalid_endpoint";
    case CommandStatus::InvalidToken: return "invalid_token";
    case CommandStatus::InvalidTtl: return "invalid_ttl";
  }
  return "invalid";
}

// One optional integer field as parsed from JSON.
struct OptionalInt {
  bool present = false;  // Present and an integer.
  long long value = 0;
};

// A hint outside its sane range keeps the default instead of rejecting.
inline StreamHints hintsFrom(OptionalInt width, OptionalInt height, OptionalInt fps,
                             OptionalInt quality) {
  StreamHints hints;
  if (width.present && height.present && width.value >= 16 && width.value <= 4096 &&
      height.value >= 8 && height.value <= 4096) {
    hints.width = static_cast<uint16_t>(width.value);
    hints.height = static_cast<uint16_t>(height.value);
  }
  if (fps.present && fps.value >= 1 && fps.value <= kMaxFps) {
    hints.fps = static_cast<uint8_t>(fps.value);
  }
  if (quality.present && quality.value >= 1 && quality.value <= 100) {
    hints.quality = static_cast<uint8_t>(quality.value);
  }
  return hints;
}

inline CommandStatus validateStreamFields(long long version, const char* session,
                                          const char* host, OptionalInt port,
                                          const char* token, OptionalInt ttl_ms,
                                          const StreamHints& hints, StreamCommand* out) {
  if (version != local_camera_contract::kProtocolVersion) {
    return CommandStatus::UnsupportedVersion;
  }
  if (!isValidSession(session)) return CommandStatus::InvalidSession;
  if (!isValidIpv4(host) || !port.present || port.value < 1 || port.value > 65535) {
    return CommandStatus::InvalidEndpoint;
  }
  if (!isValidToken(token)) return CommandStatus::InvalidToken;
  uint32_t ttl = kDefaultTtlMs;
  if (ttl_ms.present) {
    if (ttl_ms.value < kMinTtlMs || ttl_ms.value > kMaxTtlMs) return CommandStatus::InvalidTtl;
    ttl = static_cast<uint32_t>(ttl_ms.value);
  }
  if (out) {
    *out = StreamCommand{};
    snprintf(out->session, sizeof(out->session), "%s", session);
    snprintf(out->token, sizeof(out->token), "%s", token);
    snprintf(out->host, sizeof(out->host), "%s", host);
    out->port = static_cast<uint16_t>(port.value);
    out->ttl_ms = ttl;
    out->hints = hints;
  }
  return CommandStatus::Ok;
}

inline CommandStatus validateStopFields(long long version, const char* session,
                                        char* session_out, size_t session_capacity) {
  if (version != local_camera_contract::kProtocolVersion) {
    return CommandStatus::UnsupportedVersion;
  }
  if (!isValidSession(session)) return CommandStatus::InvalidSession;
  if (session_out && session_capacity) snprintf(session_out, session_capacity, "%s", session);
  return CommandStatus::Ok;
}

// ---------------------------------------------------------------------------
// Upload handshake and frame transfer
// ---------------------------------------------------------------------------
constexpr const char* kUploadPrefix = "HTCAMUP/1 ";
constexpr size_t kUploadHandshakeMaxBytes = 256;
constexpr uint32_t kHelloTimeoutMs = 5000;
// Send of one chunk plus its ACK; the Bridge itself waits 5 s per chunk.
constexpr uint32_t kChunkAckTimeoutMs = 2000;
// TEST: chunks sent ahead of their ACK (sendJpegFrame()). One chunk per round
// trip capped the upload at about 6.5 Mbit/s. Each extra chunk holds up to
// 8 KB of network buffers in the internal RAM the SDIO link and the UI share.
constexpr uint32_t kChunkWindow = 2;
constexpr uint32_t kEndSendTimeoutMs = 100;
// The Bridge closes after 10 s without a frame header; a flush keeps an idle
// but healthy connection open.
constexpr uint32_t kFlushIntervalMs = 5000;
// JPEG size limit of the Bridge receiver (MAX_JPEG_BYTES).
constexpr uint32_t kMaxFrameBytes = local_camera_contract::kPanelMaxJpegBytes;
constexpr uint32_t kMinFrameBytes = 4;

// Returns the line length, or 0 when a field is invalid or it does not fit.
inline size_t buildUploadHandshake(char* out, size_t capacity, const char* session,
                                   const char* token) {
  if (!out || capacity == 0) return 0;
  out[0] = '\0';
  if (!isValidSession(session) || !isValidToken(token)) return 0;
  const int written = snprintf(out, capacity, "%s%s %s\n", kUploadPrefix, session, token);
  if (written < 0 || static_cast<size_t>(written) >= capacity ||
      static_cast<size_t>(written) > kUploadHandshakeMaxBytes) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

// Accepts the Bridge hello: magic, status 0 and a usable chunk size.
inline bool helloAccepted(const uint8_t hello[tcp_ack::kHelloBytes], uint16_t* chunk) {
  uint16_t status = 0;
  uint16_t size = 0;
  if (!tcp_ack::decodeHello(hello, &status, &size)) return false;
  if (status != tcp_ack::kHelloAccepted || size == 0 || size > tcp_ack::kChunkBytes) {
    return false;
  }
  if (chunk) *chunk = size;
  return true;
}

// Reconnect backoff while keepalives continue: 0.25, 0.5, 1, 2, 2 ... s.
inline uint32_t reconnectDelayMs(uint32_t failures) {
  if (failures <= 1) return 250;
  if (failures == 2) return 500;
  if (failures == 3) return 1000;
  return 2000;
}

enum class FrameSendResult : uint8_t {
  Sent,
  Stopped,
  InvalidFrame,
  DmaDropped,     // Too little DMA headroom before the header: frame skipped.
  DmaSafetyStop,  // Headroom stayed low between chunks: connection must close.
  SendFailed,
  AckTimeout,
  AckClosed,
  AckError,
  BadAck,
};

inline const char* frameSendResultName(FrameSendResult result) {
  switch (result) {
    case FrameSendResult::Sent: return "sent";
    case FrameSendResult::Stopped: return "stopped";
    case FrameSendResult::InvalidFrame: return "invalid_frame";
    case FrameSendResult::DmaDropped: return "dma_dropped";
    case FrameSendResult::DmaSafetyStop: return "dma_safety_stop";
    case FrameSendResult::SendFailed: return "send_failed";
    case FrameSendResult::AckTimeout: return "ack_timeout";
    case FrameSendResult::AckClosed: return "ack_closed";
    case FrameSendResult::AckError: return "ack_error";
    case FrameSendResult::BadAck: return "bad_ack";
  }
  return "error";
}

struct FrameSendReport {
  uint32_t chunks = 0;
  uint32_t ack_ms_total = 0;
  uint32_t ack_ms_max = 0;
  uint32_t bytes_acked = 0;
};

// Non-zero deadline: the socket helpers treat 0 as "no deadline".
inline uint32_t deadlineAfter(uint32_t now_ms, uint32_t timeout_ms) {
  const uint32_t deadline = now_ms + timeout_ms;
  return deadline ? deadline : 1;
}

// Sends one JPEG frame with one chunk in flight at most. Io provides:
//   bool stopRequested();
//   uint32_t nowMs();
//   bool send(const void* data, size_t bytes, uint32_t deadline_ms);
//   tcp_ack::SocketReadResult receive(void* data, size_t bytes, uint32_t deadline_ms);
//   bool headroomForHeader();       // false: skip this frame
//   bool waitHeadroomForChunk();    // false: headroom stayed low (or stop)
//   void yieldAfterAck();
template <typename Io>
FrameSendResult sendJpegFrame(Io& io, uint32_t sequence, const uint8_t* jpeg,
                              uint32_t length, uint32_t chunk_bytes,
                              FrameSendReport* report) {
  if (!jpeg || length < kMinFrameBytes || length > kMaxFrameBytes || chunk_bytes == 0) {
    return FrameSendResult::InvalidFrame;
  }
  if (!io.headroomForHeader()) return FrameSendResult::DmaDropped;
  uint8_t header[tcp_ack::kFrameHeaderBytes];
  tcp_ack::encodeFrameHeader(header, tcp_ack::kMessageFrame, sequence, length);
  if (!io.send(header, sizeof(header), deadlineAfter(io.nowMs(), kChunkAckTimeoutMs))) {
    return io.stopRequested() ? FrameSendResult::Stopped : FrameSendResult::SendFailed;
  }
  // TEST: up to kChunkWindow chunks are in flight; the next chunk goes out
  // while the Bridge still acknowledges the previous one. The Bridge reads
  // and acknowledges chunks strictly in order; each ACK must match the oldest
  // chunk in flight. The DMA headroom check still runs before every chunk,
  // and the next frame header waits for the final ACK.
  uint32_t sent = 0;
  uint32_t offset = 0;  // Bytes acknowledged.
  uint32_t in_flight = 0;
  uint32_t sent_ms[kChunkWindow] = {};
  uint32_t first_chunk = 0;  // Index of the oldest chunk in flight.
  while (offset < length) {
    while (sent < length && in_flight < kChunkWindow) {
      if (!io.waitHeadroomForChunk()) {
        return io.stopRequested() ? FrameSendResult::Stopped : FrameSendResult::DmaSafetyStop;
      }
      const uint32_t size = length - sent < chunk_bytes ? length - sent : chunk_bytes;
      const uint32_t now_ms = io.nowMs();
      if (!io.send(jpeg + sent, size, deadlineAfter(now_ms, kChunkAckTimeoutMs))) {
        return io.stopRequested() ? FrameSendResult::Stopped : FrameSendResult::SendFailed;
      }
      sent_ms[(first_chunk + in_flight) % kChunkWindow] = now_ms;
      sent += size;
      ++in_flight;
    }
    const uint32_t started_ms = sent_ms[first_chunk % kChunkWindow];
    const uint32_t deadline_ms = deadlineAfter(started_ms, kChunkAckTimeoutMs);
    offset += length - offset < chunk_bytes ? length - offset : chunk_bytes;
    uint8_t ack[tcp_ack::kAckBytes];
    switch (io.receive(ack, sizeof(ack), deadline_ms)) {
      case tcp_ack::SocketReadResult::Ok: break;
      case tcp_ack::SocketReadResult::Stopped: return FrameSendResult::Stopped;
      case tcp_ack::SocketReadResult::Timeout: return FrameSendResult::AckTimeout;
      case tcp_ack::SocketReadResult::Closed: return FrameSendResult::AckClosed;
      default: return FrameSendResult::AckError;
    }
    uint32_t acked_sequence = 0;
    uint32_t cumulative = 0;
    if (!tcp_ack::decodeAck(ack, &acked_sequence, &cumulative) ||
        acked_sequence != sequence || cumulative != offset) {
      return FrameSendResult::BadAck;
    }
    ++first_chunk;
    --in_flight;
    if (report) {
      const uint32_t ack_ms = io.nowMs() - started_ms;
      ++report->chunks;
      report->ack_ms_total += ack_ms;
      if (ack_ms > report->ack_ms_max) report->ack_ms_max = ack_ms;
      report->bytes_acked = offset;
    }
    io.yieldAfterAck();
  }
  return FrameSendResult::Sent;
}

// Flush (keeps an idle connection open) and end (closes the upload) carry no
// payload and are never acknowledged.
template <typename Io>
bool sendControl(Io& io, uint8_t type, uint32_t sequence, uint32_t timeout_ms) {
  if (type != tcp_ack::kMessageFlush && type != tcp_ack::kMessageEnd) return false;
  uint8_t header[tcp_ack::kFrameHeaderBytes];
  tcp_ack::encodeFrameHeader(header, type, sequence, 0);
  return io.send(header, sizeof(header), deadlineAfter(io.nowMs(), timeout_ms));
}

// ---------------------------------------------------------------------------
// Image helpers
// ---------------------------------------------------------------------------

// Averages every 2x2 block of the out_w*2 x out_h*2 window at (x0, y0) of a
// packed RGB565 image (stride src_width pixels) into a packed out_w x out_h
// image. Each channel is rounded exactly like (a+b+c+d+2)/4. rotate_180
// writes the output turned by 180 degrees. The source is never modified.
inline bool downscale2x2Rgb565(const uint16_t* src, uint32_t src_width,
                               uint32_t src_height, uint32_t x0, uint32_t y0,
                               uint32_t out_w, uint32_t out_h, bool rotate_180,
                               uint16_t* out) {
  if (!src || !out || out_w == 0 || out_h == 0) return false;
  if (static_cast<uint64_t>(x0) + 2ull * out_w > src_width ||
      static_cast<uint64_t>(y0) + 2ull * out_h > src_height) {
    return false;
  }
  constexpr uint32_t kRedBlue = 0xF81Fu;
  constexpr uint32_t kGreen = 0x07E0u;
  constexpr uint32_t kRoundRedBlue = (2u << 11) | 2u;
  constexpr uint32_t kRoundGreen = 2u << 5;
  for (uint32_t oy = 0; oy < out_h; ++oy) {
    const uint16_t* row0 = src + static_cast<size_t>(y0 + 2u * oy) * src_width + x0;
    const uint16_t* row1 = row0 + src_width;
    uint16_t* dst = rotate_180 ? out + static_cast<size_t>(out_h - 1u - oy) * out_w + (out_w - 1u)
                               : out + static_cast<size_t>(oy) * out_w;
    for (uint32_t ox = 0; ox < out_w; ++ox) {
      const uint32_t a = row0[0];
      const uint32_t b = row0[1];
      const uint32_t c = row1[0];
      const uint32_t d = row1[1];
      row0 += 2;
      row1 += 2;
      // Red (bits 11-15) and blue (bits 0-4) sums cannot overlap in 32 bits.
      const uint32_t rb = (((a & kRedBlue) + (b & kRedBlue) + (c & kRedBlue) +
                            (d & kRedBlue) + kRoundRedBlue) >> 2) & kRedBlue;
      const uint32_t g = (((a & kGreen) + (b & kGreen) + (c & kGreen) + (d & kGreen) +
                           kRoundGreen) >> 2) & kGreen;
      *dst = static_cast<uint16_t>(rb | g);
      if (rotate_180) {
        --dst;
      } else {
        ++dst;
      }
    }
  }
  return true;
}

// Frame deadlines in microseconds. A deadline that is more than one period
// in the past is dropped instead of caught up (counted as late).
class FramePacer {
 public:
  void reset(uint64_t now_us, uint32_t period_us) {
    next_us_ = now_us;
    period_us_ = period_us ? period_us : 1;
  }
  uint64_t waitUs(uint64_t now_us) const { return now_us >= next_us_ ? 0 : next_us_ - now_us; }
  // Consumes the due deadline; true when at least one period was missed.
  bool consume(uint64_t now_us) {
    const bool late = now_us > next_us_ + period_us_;
    if (late) next_us_ = now_us;
    next_us_ += period_us_;
    return late;
  }
  uint32_t periodUs() const { return period_us_; }

 private:
  uint64_t next_us_ = 0;
  uint32_t period_us_ = 1;
};

inline uint32_t periodUsForFps(uint8_t fps) {
  return fps ? 1000000u / fps : 1000000u;
}

// ---------------------------------------------------------------------------
// Start/stop rules
// ---------------------------------------------------------------------------
enum class StopReason : uint8_t {
  None,
  StreamStop,   // Bridge sent stream_stop.
  Keepalive,    // ttl_ms passed without a keepalive.
  Session,      // A new session replaced this one.
  Popup,        // A camera popup stream on the display has priority.
  Disabled,     // User disabled the camera.
  Shutdown,     // OTA or restart.
  Sleep,        // Display sleep released the pipeline.
  Mqtt,         // MQTT connection lost.
  SensorUnavailable,
  Error,
  Storage,      // Web Admin storage work holds the camera off (flash stalls both cores).
};

inline const char* stopReasonName(StopReason reason) {
  switch (reason) {
    case StopReason::None: return "none";
    case StopReason::StreamStop: return "stop";
    case StopReason::Keepalive: return "keepalive";
    case StopReason::Session: return "session";
    case StopReason::Popup: return "popup";
    case StopReason::Disabled: return "disabled";
    case StopReason::Shutdown: return "shutdown";
    case StopReason::Sleep: return "sleep";
    case StopReason::Mqtt: return "mqtt";
    case StopReason::SensorUnavailable: return "sensor_unavailable";
    case StopReason::Error: return "error";
    case StopReason::Storage: return "storage";
  }
  return "error";
}

struct GateInputs {
  bool session_valid = false;
  bool ttl_expired = false;
  bool enabled = false;
  bool sensor_ready = false;
  bool mqtt_connected = false;
  bool popup_active = false;  // camera_stream_is_active(): display stream has priority.
  bool shutdown_latched = false;
  bool display_sleeping = false;  // Display sleep: keepalives defer until wake.
  bool storage_hold = false;      // Web Admin storage work: keepalives defer until it ends.
};

// First reason that forbids streaming, or None. Used both to stop a running
// upload and to decide whether a keepalive may (re)start it.
inline StopReason streamGate(const GateInputs& in) {
  if (!in.enabled) return StopReason::Disabled;
  if (in.shutdown_latched) return StopReason::Shutdown;
  if (!in.mqtt_connected) return StopReason::Mqtt;
  if (!in.session_valid) return StopReason::StreamStop;
  if (in.ttl_expired) return StopReason::Keepalive;
  if (in.display_sleeping) return StopReason::Sleep;
  if (in.storage_hold) return StopReason::Storage;
  if (in.popup_active) return StopReason::Popup;
  if (!in.sensor_ready) return StopReason::SensorUnavailable;
  return StopReason::None;
}

// Reasons after which the session is forgotten; the others (popup, sleep,
// storage, errors) resume on the next keepalive of the same session.
inline bool reasonEndsSession(StopReason reason) {
  return reason == StopReason::StreamStop || reason == StopReason::Keepalive ||
         reason == StopReason::Disabled || reason == StopReason::Shutdown ||
         reason == StopReason::Mqtt;
}

// ---------------------------------------------------------------------------
// Diagnostics: one aggregated window, never a line per frame.
// ---------------------------------------------------------------------------
constexpr uint32_t kDiagWindowMs = 10000;

struct StreamWindow {
  uint32_t window_ms = 0;
  uint32_t sent = 0;          // Frames with the final chunk acknowledged.
  uint32_t busy = 0;          // Ready frame replaced before it was sent.
  uint32_t big = 0;           // Over the Bridge size limit.
  uint32_t dma = 0;           // Too little DMA headroom before the header.
  uint32_t arb = 0;           // 2D-DMA arbiter busy.
  uint32_t late = 0;          // Deadline missed by more than one period.
  uint32_t noframe = 0;       // No complete CSI frame in time.
  uint32_t encoded = 0;
  uint32_t encode_ms_total = 0;
  uint32_t encode_ms_max = 0;
  uint32_t prep_ms_total = 0; // Crop/rotate or 2x2 scaling.
  uint64_t bytes_sent = 0;
  uint32_t chunks = 0;
  uint32_t ack_ms_total = 0;
  uint32_t ack_ms_max = 0;
  uint32_t dma_min_bytes = UINT32_MAX;
};

inline uint32_t windowSkipped(const StreamWindow& w) {
  return w.busy + w.big + w.dma + w.arb + w.late + w.noframe;
}

// value * 10 / divisor, rounded, for one-decimal output without floats.
inline uint32_t tenths(uint64_t value, uint64_t divisor) {
  return divisor ? static_cast<uint32_t>((value * 10u + divisor / 2u) / divisor) : 0;
}

inline size_t formatDiagLine(char* out, size_t capacity, const StreamSettings& settings,
                             uint8_t quality, const StreamWindow& w) {
  if (!out || capacity == 0) return 0;
  const uint32_t fps10 = tenths(static_cast<uint64_t>(w.sent) * 1000u, w.window_ms);
  const uint32_t enc_avg = w.encoded ? w.encode_ms_total / w.encoded : 0;
  const uint32_t prep_avg = w.encoded ? w.prep_ms_total / w.encoded : 0;
  const uint32_t bytes_avg = w.sent ? static_cast<uint32_t>(w.bytes_sent / w.sent) : 0;
  const uint32_t ack10 = tenths(w.ack_ms_total, w.chunks);
  // Mbit/s * 100 = bytes * 8 * 1000 / window_ms / 10000.
  const uint32_t mbit100 = w.window_ms
      ? static_cast<uint32_t>((w.bytes_sent * 8u * 100u + static_cast<uint64_t>(w.window_ms) * 500u) /
                              (static_cast<uint64_t>(w.window_ms) * 1000u))
      : 0;
  const uint32_t dma_kb = w.dma_min_bytes == UINT32_MAX ? 0 : w.dma_min_bytes / 1024u;
  const int written = snprintf(
      out, capacity,
      "[LocalCamStream] mode=%u %ux%u@%u q=%u sent=%u.%u fps skipped=%u "
      "(busy=%u big=%u dma=%u arb=%u late=%u noframe=%u) encode=%u/%u ms prep=%u ms "
      "bytes=%u ack=%u.%u/%u ms mbit=%u.%02u dma_min=%u KB",
      static_cast<unsigned>(settings.mode_id), static_cast<unsigned>(settings.width),
      static_cast<unsigned>(settings.height), static_cast<unsigned>(settings.fps),
      static_cast<unsigned>(quality), static_cast<unsigned>(fps10 / 10u),
      static_cast<unsigned>(fps10 % 10u), static_cast<unsigned>(windowSkipped(w)),
      static_cast<unsigned>(w.busy), static_cast<unsigned>(w.big),
      static_cast<unsigned>(w.dma), static_cast<unsigned>(w.arb),
      static_cast<unsigned>(w.late), static_cast<unsigned>(w.noframe),
      static_cast<unsigned>(enc_avg), static_cast<unsigned>(w.encode_ms_max),
      static_cast<unsigned>(prep_avg), static_cast<unsigned>(bytes_avg),
      static_cast<unsigned>(ack10 / 10u), static_cast<unsigned>(ack10 % 10u),
      static_cast<unsigned>(w.ack_ms_max), static_cast<unsigned>(mbit100 / 100u),
      static_cast<unsigned>(mbit100 % 100u), static_cast<unsigned>(dma_kb));
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

// /api/status local_camera.stream object (without a leading comma).
struct StreamStatus {
  bool active = false;
  bool connected = false;
  StreamSettings settings;
  uint8_t quality = 0;
  uint32_t frames_total = 0;
  uint32_t reconnects = 0;
  StopReason last_stop = StopReason::None;
  bool has_window = false;
  StreamWindow window;
};

inline size_t formatStatusJson(char* out, size_t capacity, const StreamStatus& s) {
  if (!out || capacity == 0) return 0;
  const StreamWindow& w = s.window;
  const uint32_t fps10 = tenths(static_cast<uint64_t>(w.sent) * 1000u, w.window_ms);
  const uint32_t ack10 = tenths(w.ack_ms_total, w.chunks);
  const uint32_t mbit100 = w.window_ms
      ? static_cast<uint32_t>((w.bytes_sent * 8u * 100u + static_cast<uint64_t>(w.window_ms) * 500u) /
                              (static_cast<uint64_t>(w.window_ms) * 1000u))
      : 0;
  const int written = snprintf(
      out, capacity,
      "{\"active\":%s,\"connected\":%s,\"mode\":%u,\"width\":%u,\"height\":%u,"
      "\"fps\":%u,\"quality\":%u,\"frames\":%u,\"reconnects\":%u,\"last_stop\":\"%s\","
      "\"window_ms\":%u,\"sent_fps\":%u.%u,\"skipped\":%u,\"busy\":%u,\"big\":%u,"
      "\"dma\":%u,\"arb\":%u,\"late\":%u,\"noframe\":%u,\"encode_ms\":%u,"
      "\"encode_ms_max\":%u,\"prep_ms\":%u,\"bytes_avg\":%u,\"ack_ms\":%u.%u,"
      "\"ack_ms_max\":%u,\"mbit\":%u.%02u,\"dma_min_kb\":%u}",
      s.active ? "true" : "false", s.connected ? "true" : "false",
      static_cast<unsigned>(s.settings.mode_id), static_cast<unsigned>(s.settings.width),
      static_cast<unsigned>(s.settings.height), static_cast<unsigned>(s.settings.fps),
      static_cast<unsigned>(s.quality), static_cast<unsigned>(s.frames_total),
      static_cast<unsigned>(s.reconnects), stopReasonName(s.last_stop),
      static_cast<unsigned>(s.has_window ? w.window_ms : 0),
      static_cast<unsigned>(fps10 / 10u), static_cast<unsigned>(fps10 % 10u),
      static_cast<unsigned>(windowSkipped(w)), static_cast<unsigned>(w.busy),
      static_cast<unsigned>(w.big), static_cast<unsigned>(w.dma),
      static_cast<unsigned>(w.arb), static_cast<unsigned>(w.late),
      static_cast<unsigned>(w.noframe),
      static_cast<unsigned>(w.encoded ? w.encode_ms_total / w.encoded : 0),
      static_cast<unsigned>(w.encode_ms_max),
      static_cast<unsigned>(w.encoded ? w.prep_ms_total / w.encoded : 0),
      static_cast<unsigned>(w.sent ? w.bytes_sent / w.sent : 0),
      static_cast<unsigned>(ack10 / 10u), static_cast<unsigned>(ack10 % 10u),
      static_cast<unsigned>(w.ack_ms_max), static_cast<unsigned>(mbit100 / 100u),
      static_cast<unsigned>(mbit100 % 100u),
      static_cast<unsigned>(w.dma_min_bytes == UINT32_MAX ? 0 : w.dma_min_bytes / 1024u));
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

}  // namespace local_camera_stream
