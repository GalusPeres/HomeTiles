#pragma once

// Pure, host-testable part of the acknowledged TCP camera transport shared by
// the display stream (camera_stream.cpp, Bridge -> panel) and the built-in
// camera upload (local_camera_upload.cpp, panel -> Bridge). No sockets,
// FreeRTOS or Arduino here. All integers on the wire are big-endian.
//
//   hello   ">4sHH"     8 bytes  "HTC1", status u16 (0 accepted), chunk u16
//   frame   ">4sB3xII" 16 bytes  "HTF1", type u8, 3 zero bytes, sequence u32,
//                                payload length u32
//   ack     ">4sII"    12 bytes  "HTA1", sequence u32, cumulative bytes u32
//
// The payload follows a frame header in chunks of min(chunk, remaining)
// bytes; the receiver acknowledges every chunk before the next one is sent.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace tcp_ack {

constexpr uint8_t kHelloMagic[] = {'H', 'T', 'C', '1'};
constexpr uint8_t kFrameMagic[] = {'H', 'T', 'F', '1'};
constexpr uint8_t kAckMagic[] = {'H', 'T', 'A', '1'};
constexpr uint8_t kMessageFrame = 1;
constexpr uint8_t kMessageFlush = 2;
constexpr uint8_t kMessageEnd = 3;
constexpr size_t kHelloBytes = 8;
constexpr size_t kFrameHeaderBytes = 16;
constexpr size_t kAckBytes = 12;
constexpr uint16_t kHelloAccepted = 0;
// Chunk size both directions use; the hello announces it.
constexpr size_t kChunkBytes = 8 * 1024;

// Internal DMA memory that must stay free (plus the MQTT worker's reserve)
// while a camera socket moves data through ESP-Hosted, and how long a
// shortfall is tolerated before the camera connection stops.
constexpr size_t kMinCameraDmaHeadroomBytes = 24 * 1024;
constexpr uint32_t kDmaHeadroomGraceMs = 250;

enum class SocketReadResult : uint8_t {
  Ok,
  Closed,
  Error,
  Stopped,
  Timeout,  // Only when the caller passed a deadline.
};

inline uint16_t readBe16(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                               static_cast<uint16_t>(data[1]));
}

inline uint32_t readBe32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

inline void writeBe16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value >> 8);
  data[1] = static_cast<uint8_t>(value);
}

inline void writeBe32(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value >> 24);
  data[1] = static_cast<uint8_t>(value >> 16);
  data[2] = static_cast<uint8_t>(value >> 8);
  data[3] = static_cast<uint8_t>(value);
}

inline void encodeHello(uint8_t out[kHelloBytes], uint16_t status, uint16_t chunk) {
  memcpy(out, kHelloMagic, sizeof(kHelloMagic));
  writeBe16(out + 4, status);
  writeBe16(out + 6, chunk);
}

// False when the magic does not match; status and chunk are still decoded.
inline bool decodeHello(const uint8_t in[kHelloBytes], uint16_t* status, uint16_t* chunk) {
  if (status) *status = readBe16(in + 4);
  if (chunk) *chunk = readBe16(in + 6);
  return memcmp(in, kHelloMagic, sizeof(kHelloMagic)) == 0;
}

inline void encodeFrameHeader(uint8_t out[kFrameHeaderBytes], uint8_t type,
                              uint32_t sequence, uint32_t length) {
  memcpy(out, kFrameMagic, sizeof(kFrameMagic));
  out[4] = type;
  out[5] = 0;
  out[6] = 0;
  out[7] = 0;
  writeBe32(out + 8, sequence);
  writeBe32(out + 12, length);
}

inline void encodeAck(uint8_t out[kAckBytes], uint32_t sequence, uint32_t cumulative) {
  memcpy(out, kAckMagic, sizeof(kAckMagic));
  writeBe32(out + 4, sequence);
  writeBe32(out + 8, cumulative);
}

inline bool decodeAck(const uint8_t in[kAckBytes], uint32_t* sequence, uint32_t* cumulative) {
  if (memcmp(in, kAckMagic, sizeof(kAckMagic)) != 0) return false;
  if (sequence) *sequence = readBe32(in + 4);
  if (cumulative) *cumulative = readBe32(in + 8);
  return true;
}

// Internal DMA headroom check with a short grace period, shared by both
// camera directions. headroom = free internal DMA memory + MQTT reserve.
class DmaHeadroomGuard {
 public:
  enum class Result : uint8_t { Ok, Grace, Exhausted };

  explicit DmaHeadroomGuard(size_t min_bytes = kMinCameraDmaHeadroomBytes,
                            uint32_t grace_ms = kDmaHeadroomGraceMs)
      : min_bytes_(min_bytes), grace_ms_(grace_ms) {}

  Result check(size_t headroom, uint32_t now_ms) {
    if (headroom >= min_bytes_) {
      low_since_ms_ = 0;
      return Result::Ok;
    }
    if (low_since_ms_ == 0) {
      low_since_ms_ = now_ms ? now_ms : 1;
      return Result::Grace;
    }
    if (static_cast<uint32_t>(now_ms - low_since_ms_) < grace_ms_) {
      return Result::Grace;
    }
    return Result::Exhausted;
  }

  uint32_t lowForMs(uint32_t now_ms) const {
    return low_since_ms_ ? static_cast<uint32_t>(now_ms - low_since_ms_) : 0;
  }
  void reset() { low_since_ms_ = 0; }

 private:
  size_t min_bytes_;
  uint32_t grace_ms_;
  uint32_t low_since_ms_ = 0;
};

}  // namespace tcp_ack
