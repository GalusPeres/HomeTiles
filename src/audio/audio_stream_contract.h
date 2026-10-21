#pragma once

// Pure, host-testable part of the clips the Bridge decodes for the speaker
// (Bridge audio_stream.py, docs-dev/audio-stream.md). The Bridge's FFmpeg
// reads the clip and writes mono 16-bit PCM; it reaches the panel over the
// acknowledged TCP transport of the camera stream (tcp_ack_wire.h). The play
// URL is "tcp://<host>:<port>/<token>"; the panel sends "HTAUD/1 <token>\n",
// receives the hello and then these messages (tcp_ack frame header):
//
//   type 4 format, 16 bytes big-endian: sample rate u32, channels u16 (1),
//          bits u16 (16), stream position in ms of the next sample u32,
//          clip length in ms u32 (0 = not known)
//   type 1 audio: PCM, little-endian, an even length
//   type 3 end, no payload: play the buffer out and close
//
// The panel acknowledges format and audio messages with (sequence, length)
// once they lie in its PSRAM buffer, so the Bridge never has more than its
// window (two 4 KB blocks) in flight: the internal network memory a clip
// holds is bounded whatever the clip. No sockets, FreeRTOS or Arduino here.
//
// A camera's sound comes the same way with "live":true in the play command:
// the panel keeps only a fraction of a second of it (kLiveTargetMs to
// kLiveMaxMs) so the sound stays close to the picture.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "src/video/tcp_ack_wire.h"

namespace audio_stream {

// The speaker status announces it as "stream" (speaker_contract.h).
constexpr const char* kTransport = "tcp-ack-v1";
constexpr const char* kRequestPrefix = "HTAUD/1 ";
constexpr uint8_t kMessageAudio = tcp_ack::kMessageFrame;
constexpr uint8_t kMessageEnd = tcp_ack::kMessageEnd;
constexpr uint8_t kMessageFormat = 4;
constexpr size_t kFormatBytes = 16;
// The Bridge sends 4 KB blocks (8 KB with Bridge b8); anything above this is
// not its stream.
constexpr uint32_t kMaxAudioBytes = 64u * 1024u;
constexpr uint32_t kMinRate = 8000;
constexpr uint32_t kMaxRate = 48000;
constexpr size_t kMaxHostLength = 63;
constexpr size_t kMaxTokenLength = 64;

// A camera's sound: what the panel holds beyond kLiveMaxMs (a burst after a
// Wi-Fi stall, FFmpeg's first packets) is dropped down to kLiveTargetMs.
constexpr uint32_t kLiveTargetMs = 300;
constexpr uint32_t kLiveMaxMs = 600;

// Mono 16-bit bytes for ms at rate, a whole number of samples.
inline size_t pcmBytes(uint32_t rate, uint32_t ms) {
  return static_cast<size_t>(static_cast<uint64_t>(rate) * ms / 1000u) * 2u;
}

// Bytes to drop from a live buffer holding `buffered` bytes: 0 within the
// maximum, else down to the target (whole samples).
inline size_t liveExcessBytes(size_t buffered, uint32_t rate) {
  if (buffered <= pcmBytes(rate, kLiveMaxMs)) return 0;
  return (buffered - pcmBytes(rate, kLiveTargetMs)) & ~static_cast<size_t>(1);
}

struct Endpoint {
  char host[kMaxHostLength + 1] = {};
  uint16_t port = 0;
  char token[kMaxTokenLength + 1] = {};
};

// "tcp://<host>:<port>/<token>" as the Bridge builds it: a host name, an
// IPv4 address or a bracketed IPv6 address, a port 1-65535 and a URL-safe
// base64 token.
inline bool parseStreamUrl(const char* url, size_t length, Endpoint* out) {
  if (!url || !out || length <= 6 || strncmp(url, "tcp://", 6) != 0) return false;
  *out = Endpoint{};
  size_t pos = 6;
  size_t host_start = pos;
  size_t host_end = 0;
  if (url[pos] == '[') {
    host_start = ++pos;
    while (pos < length && url[pos] != ']') {
      const char c = url[pos];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
            c == ':' || c == '.')) {
        return false;
      }
      ++pos;
    }
    if (pos >= length) return false;
    host_end = pos++;
  } else {
    while (pos < length && url[pos] != ':') {
      const char c = url[pos];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            c == '.' || c == '-')) {
        return false;
      }
      ++pos;
    }
    host_end = pos;
  }
  const size_t host_length = host_end - host_start;
  if (host_length == 0 || host_length > kMaxHostLength) return false;
  if (pos >= length || url[pos] != ':') return false;
  ++pos;
  uint32_t port = 0;
  size_t digits = 0;
  while (pos < length && url[pos] >= '0' && url[pos] <= '9') {
    port = port * 10u + static_cast<uint32_t>(url[pos] - '0');
    if (++digits > 5 || port > 65535u) return false;
    ++pos;
  }
  if (digits == 0 || port == 0 || pos >= length || url[pos] != '/') return false;
  ++pos;
  const size_t token_length = length - pos;
  if (token_length == 0 || token_length > kMaxTokenLength) return false;
  for (size_t i = pos; i < length; ++i) {
    const char c = url[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          c == '-' || c == '_')) {
      return false;
    }
  }
  memcpy(out->host, url + host_start, host_length);
  out->port = static_cast<uint16_t>(port);
  memcpy(out->token, url + pos, token_length);
  return true;
}

struct Format {
  uint32_t rate = 0;
  uint32_t position_ms = 0;
  uint32_t duration_ms = 0;
};

// Mono 16-bit PCM at a rate the output can take.
inline bool decodeFormat(const uint8_t* payload, size_t length, Format* out) {
  if (!payload || !out || length != kFormatBytes) return false;
  const uint32_t rate = tcp_ack::readBe32(payload);
  const uint16_t channels = tcp_ack::readBe16(payload + 4);
  const uint16_t bits = tcp_ack::readBe16(payload + 6);
  if (rate < kMinRate || rate > kMaxRate || channels != 1 || bits != 16) return false;
  out->rate = rate;
  out->position_ms = tcp_ack::readBe32(payload + 8);
  out->duration_ms = tcp_ack::readBe32(payload + 12);
  return true;
}

// Incremental reader of the message stream for a socket read without
// waiting: the caller asks what to read next, reads what arrived and reports
// it. Headers and the format land in the receiver's own scratch bytes, audio
// straight in the caller's buffer, so a block is never copied twice.
class Receiver {
 public:
  enum class Need : uint8_t { Header, Format, Audio, Ended, Failed };

  Need need() const { return need_; }

  // Header or format: where the next bytes go and how many are missing.
  uint8_t* scratch() { return scratch_ + scratch_used_; }
  size_t scratchMissing() const {
    return (need_ == Need::Header ? tcp_ack::kFrameHeaderBytes : kFormatBytes) - scratch_used_;
  }
  void scratchReceived(size_t bytes) {
    if (need_ != Need::Header && need_ != Need::Format) return;
    scratch_used_ += bytes < scratchMissing() ? bytes : scratchMissing();
    if (scratchMissing() > 0) return;
    scratch_used_ = 0;
    if (need_ == Need::Header) {
      onHeader();
    } else {
      onFormat();
    }
  }

  // Audio bytes of the current message still to come.
  uint32_t audioMissing() const { return need_ == Need::Audio ? audio_missing_ : 0; }
  void audioReceived(size_t bytes) {
    if (need_ != Need::Audio) return;
    audio_missing_ -= bytes < audio_missing_ ? static_cast<uint32_t>(bytes) : audio_missing_;
    if (audio_missing_ == 0) completeMessage();
  }

  // The acknowledgement owed for the last complete message, once. Send it
  // before reading on: the next complete message replaces it.
  bool takeAck(uint32_t* sequence, uint32_t* length) {
    if (!ack_due_) return false;
    ack_due_ = false;
    if (sequence) *sequence = ack_sequence_;
    if (length) *length = ack_length_;
    return true;
  }

  // The clip's format, once it arrived (only one per stream).
  bool formatReady() const { return format_ready_; }
  const Format& format() const { return format_; }

  // Why the stream failed, for the log ("" while it did not).
  const char* failure() const { return failure_; }

 private:
  void fail(const char* reason) {
    need_ = Need::Failed;
    failure_ = reason;
  }

  void onHeader() {
    if (memcmp(scratch_, tcp_ack::kFrameMagic, sizeof(tcp_ack::kFrameMagic)) != 0) {
      fail("bad_magic");
      return;
    }
    const uint8_t type = scratch_[4];
    sequence_ = tcp_ack::readBe32(scratch_ + 8);
    length_ = tcp_ack::readBe32(scratch_ + 12);
    if (type == kMessageEnd) {
      if (length_ != 0) {
        fail("bad_end");
        return;
      }
      need_ = Need::Ended;
      return;
    }
    if (type == kMessageFormat) {
      if (length_ != kFormatBytes || format_ready_) {
        fail("bad_format");
        return;
      }
      need_ = Need::Format;
      return;
    }
    if (type != kMessageAudio || !format_ready_ || length_ == 0 || (length_ & 1u) != 0 ||
        length_ > kMaxAudioBytes) {
      fail("bad_audio");
      return;
    }
    audio_missing_ = length_;
    need_ = Need::Audio;
  }

  void onFormat() {
    if (!decodeFormat(scratch_, kFormatBytes, &format_)) {
      fail("unsupported_format");
      return;
    }
    format_ready_ = true;
    completeMessage();
  }

  void completeMessage() {
    ack_due_ = true;
    ack_sequence_ = sequence_;
    ack_length_ = length_;
    need_ = Need::Header;
  }

  Need need_ = Need::Header;
  uint8_t scratch_[tcp_ack::kFrameHeaderBytes] = {};
  size_t scratch_used_ = 0;
  uint32_t sequence_ = 0;
  uint32_t length_ = 0;
  uint32_t audio_missing_ = 0;
  bool ack_due_ = false;
  uint32_t ack_sequence_ = 0;
  uint32_t ack_length_ = 0;
  bool format_ready_ = false;
  Format format_;
  const char* failure_ = "";
};

static_assert(tcp_ack::kFrameHeaderBytes >= kFormatBytes, "the format fits the scratch bytes");

}  // namespace audio_stream
