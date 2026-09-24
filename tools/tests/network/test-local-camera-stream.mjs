// Host regression for the built-in camera live stream (panel -> Bridge): the
// shared acknowledged-TCP wire structs, the HTCAMUP handshake, the per-chunk
// ACK loop against a fake Bridge socket, the MQTT stream command validation,
// the mode table and its resolution, the 2x2 RGB565 downscale, frame pacing,
// the start/stop gate (camera popup preemption) and the diagnostic formats.
// A second program exercises the ArduinoJson command parser when the pinned
// library is installed.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const out = path.join(root, 'build/tests/local-camera-stream');
fs.mkdirSync(out, {recursive: true});

function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: local camera stream test needs a host C++ compiler');
  process.exit(0);
}

function compileAndRun(name, source, includes) {
  const cpp = path.join(out, `${name}.cpp`);
  const exe = path.join(out, process.platform === 'win32' ? `${name}.exe` : name);
  fs.writeFileSync(cpp, source);
  const args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root];
  for (const include of includes) args.push('-I', include);
  const build = spawnSync(compiler, [...args, cpp, '-o', exe], {encoding: 'utf8'});
  assert.equal(build.status, 0, `${name} did not compile:\n${build.stdout}${build.stderr}`);
  const run = spawnSync(exe, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, `${name} failed:\n${run.stdout}${run.stderr}`);
  return run.stdout;
}

const program = String.raw`
#include "src/video/local_camera/local_camera_stream_contract.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

using namespace local_camera_stream;
using tcp_ack::SocketReadResult;

// Minimal Bridge receiver (local_camera_stream.py _async_receive_frame): it
// parses what the panel sends and queues one ACK per complete chunk. It fails
// the test when the panel sends payload while an ACK is still unread.
struct FakeBridge {
  std::vector<uint8_t> wire;     // Everything the panel sent.
  std::deque<uint8_t> acks;      // Bytes the panel can read.
  uint32_t chunk = 8192;
  // Receiver state.
  bool in_frame = false;
  uint32_t sequence = 0;
  uint32_t length = 0;
  uint32_t received = 0;
  uint32_t chunk_fill = 0;
  std::vector<uint8_t> header;
  std::vector<std::vector<uint8_t>> frames;
  std::vector<uint8_t> payload;
  int flushes = 0;
  int ends = 0;
  bool violation = false;
  // Fault injection.
  size_t max_unread_acks = 0;
  bool corrupt_ack = false;
  bool never_ack = false;
  bool close_after_first_chunk = false;
  bool closed = false;

  void feed(uint8_t byte) {
    wire.push_back(byte);
    if (!in_frame) {
      header.push_back(byte);
      if (header.size() < tcp_ack::kFrameHeaderBytes) return;
      if (memcmp(header.data(), "HTF1", 4) != 0) violation = true;
      const uint8_t type = header[4];
      if (header[5] || header[6] || header[7]) violation = true;
      sequence = tcp_ack::readBe32(header.data() + 8);
      length = tcp_ack::readBe32(header.data() + 12);
      header.clear();
      if (type == tcp_ack::kMessageFlush || type == tcp_ack::kMessageEnd) {
        if (length != 0) violation = true;
        if (type == tcp_ack::kMessageFlush) ++flushes; else ++ends;
        return;
      }
      if (type != tcp_ack::kMessageFrame || length < 4 || length > kMaxFrameBytes) violation = true;
      in_frame = true;
      received = 0;
      chunk_fill = 0;
      payload.clear();
      return;
    }
    // Chunks sent ahead of their ACK: never more than the window.
    if (chunk_fill == 0) {
      const size_t unread = acks.size() / tcp_ack::kAckBytes;
      if (unread > max_unread_acks) max_unread_acks = unread;
      if (unread >= kChunkWindow) violation = true;
    }
    payload.push_back(byte);
    ++received;
    ++chunk_fill;
    const uint32_t expected = std::min<uint32_t>(chunk, length - (received - chunk_fill));
    if (chunk_fill == expected) {
      chunk_fill = 0;
      if (received == chunk_fill + expected && payload[0] != 0xFF) violation = true;
      if (!never_ack) {
        uint8_t ack[tcp_ack::kAckBytes];
        tcp_ack::encodeAck(ack, corrupt_ack ? sequence + 1 : sequence, received);
        acks.insert(acks.end(), ack, ack + sizeof(ack));
      }
      if (close_after_first_chunk) closed = true;
      if (received == length) {
        in_frame = false;
        frames.push_back(payload);
      }
    }
  }
};

struct FakeIo {
  explicit FakeIo(FakeBridge* target) : bridge(target) {}
  FakeBridge* bridge;
  uint32_t now = 1000;
  bool stop = false;
  bool header_headroom = true;
  int chunk_headroom_failures_at = -1;  // Chunk index whose headroom check fails.
  int headroom_checks = 0;
  int yields = 0;

  bool stopRequested() { return stop; }
  uint32_t nowMs() { now += 3; return now; }
  bool send(const void* data, size_t bytes, uint32_t deadline_ms) {
    assert(deadline_ms != 0);
    if (stop) return false;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) bridge->feed(p[i]);
    return true;
  }
  SocketReadResult receive(void* data, size_t bytes, uint32_t deadline_ms) {
    assert(deadline_ms != 0);
    if (stop) return SocketReadResult::Stopped;
    if (bridge->closed && bridge->acks.empty()) return SocketReadResult::Closed;
    if (bridge->acks.size() < bytes) return SocketReadResult::Timeout;
    uint8_t* p = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
      p[i] = bridge->acks.front();
      bridge->acks.pop_front();
    }
    if (bridge->close_after_first_chunk) bridge->closed = true;
    return SocketReadResult::Ok;
  }
  bool headroomForHeader() { return header_headroom; }
  bool waitHeadroomForChunk() { return headroom_checks++ != chunk_headroom_failures_at; }
  void yieldAfterAck() { ++yields; }
};

static std::vector<uint8_t> jpeg(size_t bytes) {
  std::vector<uint8_t> data(bytes);
  for (size_t i = 0; i < bytes; ++i) data[i] = static_cast<uint8_t>(i * 7 + 3);
  data[0] = 0xFF; data[1] = 0xD8; data[bytes - 2] = 0xFF; data[bytes - 1] = 0xD9;
  return data;
}

static uint16_t rgb(uint32_t r, uint32_t g, uint32_t b) {
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

int main() {
  // --- Wire structs: byte-exact with Python >4sHH, >4sB3xII, >4sII. -------
  uint8_t hello[8];
  tcp_ack::encodeHello(hello, 0, 8192);
  const uint8_t hello_expected[] = {'H','T','C','1',0x00,0x00,0x20,0x00};
  assert(memcmp(hello, hello_expected, 8) == 0);
  uint16_t chunk = 0;
  assert(helloAccepted(hello, &chunk) && chunk == 8192);
  tcp_ack::encodeHello(hello, 1, 8192);
  assert(!helloAccepted(hello, &chunk));                 // Rejected by the Bridge.
  tcp_ack::encodeHello(hello, 0, 0);
  assert(!helloAccepted(hello, &chunk));
  tcp_ack::encodeHello(hello, 0, 16384);
  assert(!helloAccepted(hello, &chunk));                 // Larger than our chunk.
  tcp_ack::encodeHello(hello, 0, 4096);
  assert(helloAccepted(hello, &chunk) && chunk == 4096); // Smaller is honoured.
  hello[0] = 'X';
  assert(!helloAccepted(hello, &chunk));

  uint8_t header[16];
  tcp_ack::encodeFrameHeader(header, tcp_ack::kMessageFrame, 0x01020304, 20000);
  const uint8_t header_expected[] = {'H','T','F','1',1,0,0,0,1,2,3,4,0x00,0x00,0x4E,0x20};
  assert(memcmp(header, header_expected, 16) == 0);
  uint8_t ack[12];
  tcp_ack::encodeAck(ack, 7, 16384);
  const uint8_t ack_expected[] = {'H','T','A','1',0,0,0,7,0,0,0x40,0};
  assert(memcmp(ack, ack_expected, 12) == 0);
  uint32_t seq = 0, cum = 0;
  assert(tcp_ack::decodeAck(ack, &seq, &cum) && seq == 7 && cum == 16384);
  ack[3] = '2';
  assert(!tcp_ack::decodeAck(ack, &seq, &cum));

  // --- Handshake line. -----------------------------------------------------
  char line[300];
  const char* session = "0123456789abcdef0123456789abcdef";
  const char* token = "fedcba9876543210fedcba9876543210";
  const size_t line_bytes = buildUploadHandshake(line, sizeof(line), session, token);
  assert(std::string(line) ==
         "HTCAMUP/1 0123456789abcdef0123456789abcdef fedcba9876543210fedcba9876543210\n");
  assert(line_bytes == strlen(line) && line_bytes <= kUploadHandshakeMaxBytes);
  assert(buildUploadHandshake(line, sizeof(line), "ABCDEF0123456789", token) == 0);
  assert(buildUploadHandshake(line, sizeof(line), session, "short") == 0);
  assert(buildUploadHandshake(line, 20, session, token) == 0 && line[0] == '\0');

  // --- Chunk/ACK loop against the fake Bridge. -------------------------------
  {
    FakeBridge bridge;
    FakeIo io{&bridge};
    const std::vector<uint8_t> frame = jpeg(20000);  // 8192 + 8192 + 3616
    FrameSendReport report;
    assert(sendJpegFrame(io, 42, frame.data(), 20000, 8192, &report) == FrameSendResult::Sent);
    assert(!bridge.violation);
    assert(bridge.frames.size() == 1 && bridge.frames[0] == frame);
    assert(bridge.acks.empty());                     // Every ACK was consumed.
    assert(report.chunks == 3 && report.bytes_acked == 20000);
    assert(io.yields == 3 && io.headroom_checks == 3);
    assert(bridge.max_unread_acks == kChunkWindow - 1);
    assert(bridge.wire.size() == 16 + 20000);
    assert(memcmp(bridge.wire.data(), "HTF1\x01\0\0\0\0\0\0\x2a\0\0\x4e\x20", 16) == 0);
    // Exact multiple of the chunk size and a single short chunk.
    const std::vector<uint8_t> exact = jpeg(16384);
    assert(sendJpegFrame(io, 43, exact.data(), 16384, 8192, &report) == FrameSendResult::Sent);
    const std::vector<uint8_t> tiny = jpeg(4);
    assert(sendJpegFrame(io, 44, tiny.data(), 4, 8192, nullptr) == FrameSendResult::Sent);
    // A smaller announced chunk is honoured.
    bridge.chunk = 4096;
    assert(sendJpegFrame(io, 45, frame.data(), 20000, 4096, &report) == FrameSendResult::Sent);
    assert(!bridge.violation && bridge.frames.size() == 4 && bridge.frames[3] == frame);
    // Flush and end: 16-byte headers with length 0, never acknowledged.
    assert(sendControl(io, tcp_ack::kMessageFlush, 45, 100));
    assert(sendControl(io, tcp_ack::kMessageEnd, 45, 100));
    assert(!sendControl(io, tcp_ack::kMessageFrame, 45, 100));
    assert(bridge.flushes == 1 && bridge.ends == 1 && bridge.acks.empty());
  }
  {
    // Invalid frames never reach the socket.
    FakeBridge bridge;
    FakeIo io{&bridge};
    const std::vector<uint8_t> big = jpeg(kMaxFrameBytes + 1);
    assert(sendJpegFrame(io, 1, big.data(), kMaxFrameBytes + 1, 8192, nullptr) ==
           FrameSendResult::InvalidFrame);
    assert(sendJpegFrame(io, 1, big.data(), 3, 8192, nullptr) == FrameSendResult::InvalidFrame);
    assert(sendJpegFrame(io, 1, nullptr, 100, 8192, nullptr) == FrameSendResult::InvalidFrame);
    assert(bridge.wire.empty());
    // The Bridge maximum itself is allowed (16 chunks).
    const std::vector<uint8_t> max = jpeg(kMaxFrameBytes);
    FrameSendReport report;
    assert(sendJpegFrame(io, 2, max.data(), kMaxFrameBytes, 8192, &report) == FrameSendResult::Sent);
    assert(report.chunks == 16 && !bridge.violation);
    assert(bridge.max_unread_acks == kChunkWindow - 1);
  }
  {
    // Too little DMA headroom before the header: the frame is skipped cleanly.
    FakeBridge bridge;
    FakeIo io{&bridge};
    io.header_headroom = false;
    const std::vector<uint8_t> frame = jpeg(9000);
    assert(sendJpegFrame(io, 1, frame.data(), 9000, 8192, nullptr) == FrameSendResult::DmaDropped);
    assert(bridge.wire.empty());
    // Headroom that stays low between chunks ends the connection.
    io.header_headroom = true;
    io.chunk_headroom_failures_at = 1;
    assert(sendJpegFrame(io, 2, frame.data(), 9000, 8192, nullptr) ==
           FrameSendResult::DmaSafetyStop);
    assert(bridge.wire.size() == 16 + 8192);  // Nothing after the first chunk.
  }
  {
    // ACK faults.
    const std::vector<uint8_t> frame = jpeg(12000);
    FakeBridge wrong;
    wrong.corrupt_ack = true;
    FakeIo io_wrong{&wrong};
    assert(sendJpegFrame(io_wrong, 5, frame.data(), 12000, 8192, nullptr) == FrameSendResult::BadAck);
    assert(wrong.wire.size() == 16 + 12000);  // Both chunks were in flight.
    FakeBridge silent;
    silent.never_ack = true;
    FakeIo io_silent{&silent};
    assert(sendJpegFrame(io_silent, 5, frame.data(), 12000, 8192, nullptr) ==
           FrameSendResult::AckTimeout);
    FakeBridge closing;
    closing.never_ack = true;
    closing.close_after_first_chunk = true;
    FakeIo io_closing{&closing};
    assert(sendJpegFrame(io_closing, 5, frame.data(), 12000, 8192, nullptr) ==
           FrameSendResult::AckClosed);
    FakeBridge stopped;
    FakeIo io_stopped{&stopped};
    io_stopped.stop = true;
    assert(sendJpegFrame(io_stopped, 5, frame.data(), 12000, 8192, nullptr) ==
           FrameSendResult::Stopped);
  }

  // --- DMA headroom guard shared with the display stream. ------------------
  {
    tcp_ack::DmaHeadroomGuard guard(24 * 1024, 250);
    using R = tcp_ack::DmaHeadroomGuard::Result;
    assert(guard.check(30000, 100) == R::Ok);
    assert(guard.check(1000, 200) == R::Grace);
    assert(guard.check(1000, 449) == R::Grace);
    assert(guard.lowForMs(449) == 249);
    assert(guard.check(1000, 450) == R::Exhausted);
    assert(guard.check(30000, 460) == R::Ok);    // Recovery resets the timer.
    assert(guard.check(1000, 470) == R::Grace);
    // Wrap-safe across the millis() overflow.
    assert(guard.check(30000, 480) == R::Ok);
    assert(guard.check(1000, 0xFFFFFFF0u) == R::Grace);
    assert(guard.check(1000, 0x00000050u) == R::Grace);
    assert(guard.check(1000, 0x00000100u) == R::Exhausted);
  }

  // --- Mode table: stable, unique, append-only ids. -------------------------
  {
    // A mode is a rate and a start quality for the board's own full image.
    const struct { uint8_t id; uint8_t fps, q; } expected[] = {
        {1, 5, 65}, {2, 10, 45}, {3, 15, 50}, {4, 20, 45}, {5, 25, 40}};
    assert(kModeCount == 5);
    for (size_t i = 0; i < kModeCount; ++i) {
      assert(kModes[i].id == expected[i].id && kModes[i].fps == expected[i].fps &&
             kModes[i].quality == expected[i].q);
      assert(findMode(kModes[i].id) == &kModes[i]);
      assert(kModes[i].id != kModeAuto);
    }
    assert(maxTableFps() == 25);
    assert(isKnownMode(0) && isKnownMode(5) && !isKnownMode(6) && !isKnownMode(255));
    // Custom: its own id outside the table, user fps (1..25) and quality.
    assert(kModeCustom == 100 && isKnownMode(kModeCustom) && findMode(kModeCustom) == nullptr);
    char label[48];
    assert(formatModeLabel(label, sizeof(label), kModes[0], 1280, 720) &&
           std::string(label) == "1280x720, 5 fps, q65");
    assert(formatModeLabel(label, sizeof(label), kModes[4], 960, 544) &&
           std::string(label) == "960x544, 25 fps, q40");

    StreamSettings s;
    StreamHints hints;  // Bridge default: 640x360@15 q65.
    for (size_t i = 0; i < kModeCount; ++i) {
      assert(resolveSettings(kModes[i].id, hints, 1280, 720, &s));
      assert(s.mode_id == kModes[i].id && s.width == 1280 && s.height == 720 &&
             s.fps == kModes[i].fps && s.quality == kModes[i].quality);
      assert(!s.half);  // Every mode is the full sensor image.
    }
    // Regression b26: the Waveshare 8-inch image (960x544) had no table entry
    // and every mode, Auto and Custom ran at 5 fps. Every size gets the table.
    for (size_t i = 0; i < kModeCount; ++i) {
      assert(resolveSettings(kModes[i].id, hints, 960, 544, &s));
      assert(s.mode_id == kModes[i].id && s.width == 960 && s.height == 544 &&
             s.fps == kModes[i].fps && s.quality == kModes[i].quality && !s.half);
    }
    assert(resolveSettings(kModeAuto, hints, 960, 544, &s) && s.fps == 15 && s.quality == 50);
    {
      CustomMode custom;
      custom.fps = 20;
      custom.quality = 60;
      assert(resolveSettings(kModeCustom, hints, 960, 544, &s, custom) && s.fps == 20 &&
             s.width == 960 && s.height == 544);
    }
    // Auto streams the full image; fps follows the hint, the quality stays at
    // or below the table mode for that rate.
    assert(resolveSettings(kModeAuto, hints, 1280, 720, &s));
    assert(s.mode_id == 0 && s.width == 1280 && s.height == 720 && !s.half && s.fps == 15 &&
           s.quality == 50);
    hints.width = 1280; hints.height = 720; hints.fps = 24; hints.quality = 95;
    assert(resolveSettings(kModeAuto, hints, 1280, 720, &s));
    assert(s.width == 1280 && !s.half && s.fps == 24 && s.quality == 40);  // 25 fps table mode.
    hints.fps = 60;
    assert(resolveSettings(kModeAuto, hints, 1280, 720, &s) && s.fps == 25 && s.quality == 40);
    hints.width = 800; hints.height = 450; hints.fps = 30; hints.quality = 10;
    assert(resolveSettings(kModeAuto, hints, 1280, 720, &s));
    assert(s.width == 1280 && s.fps == 25 && s.quality == kMinQuality);  // Hinted size ignored.
    hints.fps = 3; hints.quality = 90;
    assert(resolveSettings(kModeAuto, hints, 1280, 720, &s) && s.fps == 3 && s.quality == 65);
    // Custom streams the full image at the user's rate and quality, clamped.
    {
      CustomMode custom;
      custom.fps = 1; custom.quality = 80;
      assert(resolveSettings(kModeCustom, StreamHints{}, 1280, 720, &s, custom));
      assert(s.mode_id == kModeCustom && s.width == 1280 && s.height == 720 && !s.half &&
             s.fps == 1 && s.quality == 80);
      custom.fps = 60; custom.quality = 5;
      assert(resolveSettings(kModeCustom, StreamHints{}, 1280, 720, &s, custom));
      assert(s.fps == 25 && s.quality == kCustomMinQuality && kCustomMinQuality == 10);
      custom.quality = 10;
      assert(resolveSettings(kModeCustom, StreamHints{}, 1280, 720, &s, custom) && s.quality == 10);
      custom.fps = 0; custom.quality = 200;
      assert(resolveSettings(kModeCustom, StreamHints{}, 1280, 720, &s, custom));
      assert(s.fps == 1 && s.quality == kMaxQuality);
      // Defaults when nothing was stored: 1 fps, q65.
      assert(resolveSettings(kModeCustom, StreamHints{}, 1280, 720, &s));
      assert(s.fps == 1 && s.quality == 65);
      const CustomMode made = makeCustomMode(-5, 1000);
      assert(made.fps == kCustomMinFps && made.quality == kMaxQuality);
      assert(!resolveSettings(kModeCustom, StreamHints{}, 100, 50, &s, custom));
    }
    // Unknown stored ids behave like Auto.
    assert(resolveSettings(99, StreamHints{}, 1280, 720, &s) && s.mode_id == 0 && s.width == 1280);
    // A board whose image is not 1280x720 streams its own full image at the mode rate.
    assert(resolveSettings(1, StreamHints{}, 1920, 1088, &s) && s.width == 1920 && s.height == 1088 &&
           s.fps == 5);
    assert(resolveSettings(3, StreamHints{}, 1920, 1088, &s) && s.width == 1920 && s.fps == 15);
    assert(!resolveSettings(0, StreamHints{}, 100, 50, &s));

    assert(reducedQuality(65) == 60 && reducedQuality(34) == 30 && reducedQuality(30) == 30);
    // Oversized frames go below the normal floor, down to 10.
    assert(reducedQualityForSize(40) == 35 && reducedQualityForSize(30) == 25);
    assert(reducedQualityForSize(14) == 10 && reducedQualityForSize(10) == 10);
    // Exposure limits keep the frame interval (34 ms frame at 1132 lines).
    assert(maxExposureLinesForFps(30, 34, 1132, 4624) == 1132);
    assert(maxExposureLinesForFps(25, 34, 1132, 4624) == 1331);
    assert(maxExposureLinesForFps(15, 34, 1132, 4624) == 2197);
    assert(maxExposureLinesForFps(5, 34, 1132, 4624) == 4624);
    for (uint32_t f = 1; f < 10; ++f) assert(reconnectDelayMs(f) >= 250 && reconnectDelayMs(f) <= 2000);
    assert(reconnectDelayMs(1) == 250 && reconnectDelayMs(2) == 500 && reconnectDelayMs(3) == 1000 &&
           reconnectDelayMs(7) == 2000);
  }

  // --- 2x2 downscale against a per-channel reference. ----------------------
  {
    const uint32_t sw = 1288, sh = 728, ow = 640, oh = 360, x0 = 4, y0 = 4;
    std::vector<uint16_t> src(sw * sh);
    uint32_t state = 12345;
    for (auto& px : src) {
      state = state * 1103515245u + 12345u;
      px = static_cast<uint16_t>(state >> 16);
    }
    // Saturated corner blocks exercise the carry between channels.
    src[y0 * sw + x0] = src[y0 * sw + x0 + 1] = src[(y0 + 1) * sw + x0] =
        src[(y0 + 1) * sw + x0 + 1] = 0xFFFF;
    std::vector<uint16_t> dst(ow * oh), rot(ow * oh);
    assert(downscale2x2Rgb565(src.data(), sw, sh, x0, y0, ow, oh, false, dst.data()));
    assert(downscale2x2Rgb565(src.data(), sw, sh, x0, y0, ow, oh, true, rot.data()));
    for (uint32_t y = 0; y < oh; ++y) {
      for (uint32_t x = 0; x < ow; ++x) {
        const uint16_t p[4] = {src[(y0 + 2 * y) * sw + x0 + 2 * x],
                               src[(y0 + 2 * y) * sw + x0 + 2 * x + 1],
                               src[(y0 + 2 * y + 1) * sw + x0 + 2 * x],
                               src[(y0 + 2 * y + 1) * sw + x0 + 2 * x + 1]};
        uint32_t r = 2, g = 2, b = 2;
        for (uint16_t v : p) { r += v >> 11; g += (v >> 5) & 63; b += v & 31; }
        const uint16_t expected = rgb(r >> 2, g >> 2, b >> 2);
        assert(dst[y * ow + x] == expected);
        assert(rot[(oh - 1 - y) * ow + (ow - 1 - x)] == expected);  // Turned by 180 degrees.
      }
    }
    assert(dst[0] == 0xFFFF);
    // The window must fit the source.
    assert(!downscale2x2Rgb565(src.data(), sw, sh, 9, 4, ow, oh, false, dst.data()));
    assert(!downscale2x2Rgb565(src.data(), sw, sh, 4, 9, ow, oh, false, dst.data()));
    assert(!downscale2x2Rgb565(nullptr, sw, sh, 4, 4, ow, oh, false, dst.data()));
  }

  // --- Frame pacing: deadlines, never catch up. -----------------------------
  {
    FramePacer pacer;
    const uint32_t period = periodUsForFps(15);
    assert(period == 66666);
    pacer.reset(1000000, period);
    assert(pacer.waitUs(1000000) == 0);
    assert(!pacer.consume(1000000));
    assert(pacer.waitUs(1000000) == period);
    assert(!pacer.consume(1000000 + period + 5000));  // Slightly late: keep the grid.
    assert(pacer.waitUs(1000000 + period + 5000) == period - 5000);
    // More than one period late: skip, count, restart from now.
    assert(pacer.consume(5000000));
    assert(pacer.waitUs(5000000) == period);
  }

  // --- MQTT stream command validation. -------------------------------------
  {
    assert(isValidIpv4("192.168.1.20") && isValidIpv4("10.0.0.1"));
    assert(!isValidIpv4("0.0.0.0") && !isValidIpv4("224.0.0.1") && !isValidIpv4("239.1.1.1"));
    assert(!isValidIpv4("192.168.1") && !isValidIpv4("192.168.1.256") && !isValidIpv4("1.2.3.4.5"));
    assert(!isValidIpv4("01.2.3.4") && !isValidIpv4("a.b.c.d") && !isValidIpv4("1..2.3") &&
           !isValidIpv4("") && !isValidIpv4(nullptr) && !isValidIpv4("::1"));
    assert(isValidSession("0123456789abcdef") && !isValidSession("0123456789abcde"));
    assert(!isValidSession("0123456789ABCDEF") && !isValidSession(nullptr));
    assert(isValidToken("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    assert(!isValidToken("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0"));

    OptionalInt port{true, 8124}, ttl{true, 6000}, none{};
    StreamCommand command;
    assert(validateStreamFields(1, session, "192.168.1.20", port, token, ttl, StreamHints{},
                                &command) == CommandStatus::Ok);
    assert(std::string(command.session) == session && std::string(command.token) == token &&
           std::string(command.host) == "192.168.1.20" && command.port == 8124 &&
           command.ttl_ms == 6000);
    assert(validateStreamFields(1, session, "192.168.1.20", port, token, none, StreamHints{},
                                &command) == CommandStatus::Ok && command.ttl_ms == kDefaultTtlMs);
    assert(validateStreamFields(2, session, "192.168.1.20", port, token, ttl, StreamHints{},
                                nullptr) == CommandStatus::UnsupportedVersion);
    assert(validateStreamFields(1, "bad", "192.168.1.20", port, token, ttl, StreamHints{},
                                nullptr) == CommandStatus::InvalidSession);
    assert(validateStreamFields(1, session, "example.org", port, token, ttl, StreamHints{},
                                nullptr) == CommandStatus::InvalidEndpoint);
    assert(validateStreamFields(1, session, "192.168.1.20", OptionalInt{true, 0}, token, ttl,
                                StreamHints{}, nullptr) == CommandStatus::InvalidEndpoint);
    assert(validateStreamFields(1, session, "192.168.1.20", OptionalInt{true, 65536}, token, ttl,
                                StreamHints{}, nullptr) == CommandStatus::InvalidEndpoint);
    assert(validateStreamFields(1, session, "192.168.1.20", none, token, ttl, StreamHints{},
                                nullptr) == CommandStatus::InvalidEndpoint);
    assert(validateStreamFields(1, session, "192.168.1.20", port, "zz", ttl, StreamHints{},
                                nullptr) == CommandStatus::InvalidToken);
    assert(validateStreamFields(1, session, "192.168.1.20", port, token, OptionalInt{true, 500},
                                StreamHints{}, nullptr) == CommandStatus::InvalidTtl);
    assert(validateStreamFields(1, session, "192.168.1.20", port, token, OptionalInt{true, 60001},
                                StreamHints{}, nullptr) == CommandStatus::InvalidTtl);
    char stop_session[33];
    assert(validateStopFields(1, session, stop_session, sizeof(stop_session)) == CommandStatus::Ok &&
           std::string(stop_session) == session);
    assert(validateStopFields(1, "nope", stop_session, sizeof(stop_session)) ==
           CommandStatus::InvalidSession);

    // Hints never reject: out-of-range values keep the defaults.
    StreamHints h = hintsFrom({true, 1280}, {true, 720}, {true, 10}, {true, 45});
    assert(h.width == 1280 && h.height == 720 && h.fps == 10 && h.quality == 45);
    h = hintsFrom({true, 99999}, {true, 720}, {true, 0}, {true, 101});
    assert(h.width == 640 && h.height == 360 && h.fps == 15 && h.quality == 65);
    h = hintsFrom({}, {}, {}, {});
    assert(sameHints(h, StreamHints{}));
  }

  // --- Start/stop gate: the display camera popup has priority. -------------
  {
    GateInputs in;
    in.session_valid = true;
    in.enabled = true;
    in.sensor_ready = true;
    in.mqtt_connected = true;
    assert(streamGate(in) == StopReason::None);
    GateInputs popup = in;
    popup.popup_active = true;
    assert(streamGate(popup) == StopReason::Popup);   // Stops and never starts.
    assert(!reasonEndsSession(StopReason::Popup));    // Resumes on the next keepalive.
    GateInputs ttl = in;
    ttl.ttl_expired = true;
    assert(streamGate(ttl) == StopReason::Keepalive && reasonEndsSession(StopReason::Keepalive));
    GateInputs mqtt = in;
    mqtt.mqtt_connected = false;
    assert(streamGate(mqtt) == StopReason::Mqtt && reasonEndsSession(StopReason::Mqtt));
    GateInputs disabled = popup;
    disabled.enabled = false;
    assert(streamGate(disabled) == StopReason::Disabled && reasonEndsSession(StopReason::Disabled));
    GateInputs ota = in;
    ota.shutdown_latched = true;
    assert(streamGate(ota) == StopReason::Shutdown && reasonEndsSession(StopReason::Shutdown));
    GateInputs sensor = in;
    sensor.sensor_ready = false;
    assert(streamGate(sensor) == StopReason::SensorUnavailable);
    assert(!reasonEndsSession(StopReason::Sleep) && reasonEndsSession(StopReason::StreamStop));
    // Display sleep stops the upload and defers keepalives until wake, but the
    // session survives so the first keepalive after wake resumes it.
    GateInputs sleeping = in;
    sleeping.display_sleeping = true;
    assert(streamGate(sleeping) == StopReason::Sleep);
    GateInputs sleeping_popup = sleeping;
    sleeping_popup.popup_active = true;
    assert(streamGate(sleeping_popup) != StopReason::None);
    GateInputs sleeping_ttl = sleeping;
    sleeping_ttl.ttl_expired = true;
    assert(streamGate(sleeping_ttl) == StopReason::Keepalive);  // ttl still ends it.
    assert(std::string(stopReasonName(StopReason::Sleep)) == "sleep");
    GateInputs no_session = in;
    no_session.session_valid = false;
    assert(streamGate(no_session) != StopReason::None);
    assert(std::string(stopReasonName(StopReason::Popup)) == "popup");
    // Web Admin storage work stops the upload like display sleep: keepalives
    // defer, the session survives and resumes afterwards.
    GateInputs storage = in;
    storage.storage_hold = true;
    assert(streamGate(storage) == StopReason::Storage);
    assert(!reasonEndsSession(StopReason::Storage));
    assert(std::string(stopReasonName(StopReason::Storage)) == "storage");
    GateInputs storage_ttl = storage;
    storage_ttl.ttl_expired = true;
    assert(streamGate(storage_ttl) == StopReason::Keepalive);  // ttl still ends it.
  }

  // --- Diagnostics: one aggregated line and the status JSON. ---------------
  {
    StreamSettings settings;
    settings.mode_id = 3; settings.width = 640; settings.height = 360; settings.fps = 15;
    StreamWindow w;
    w.window_ms = 10000; w.sent = 148; w.busy = 1; w.dma = 1; w.encoded = 150;
    w.encode_ms_total = 1800; w.encode_ms_max = 21; w.prep_ms_total = 1350;
    w.bytes_sent = 148ull * 34000u; w.chunks = 740; w.ack_ms_total = 3034; w.ack_ms_max = 12;
    w.dma_min_bytes = 41 * 1024 + 100;
    char line[320];
    assert(formatDiagLine(line, sizeof(line), settings, 65, w));
    assert(std::string(line) ==
           "[LocalCamStream] mode=3 640x360@15 q=65 sent=14.8 fps skipped=2 "
           "(busy=1 big=0 dma=1 arb=0 late=0 noframe=0) encode=12/21 ms prep=9 ms "
           "bytes=34000 ack=4.1/12 ms mbit=4.03 dma_min=41 KB");
    StreamStatus status;
    status.active = true; status.connected = true; status.settings = settings; status.quality = 65;
    status.frames_total = 999; status.has_window = true; status.window = w;
    char json[640];
    assert(formatStatusJson(json, sizeof(json), status));
    const std::string text(json);
    assert(text.rfind("{\"active\":true,\"connected\":true,\"mode\":3,\"width\":640", 0) == 0);
    assert(text.find("\"sent_fps\":14.8") != std::string::npos);
    assert(text.find("\"mbit\":4.03") != std::string::npos);
    assert(text.find("\"last_stop\":\"none\"") != std::string::npos);
    assert(text.find("\"dma_min_kb\":41}") != std::string::npos);
    StreamStatus idle;
    assert(formatStatusJson(json, sizeof(json), idle));
    assert(std::string(json).find("\"dma_min_kb\":0}") != std::string::npos);
    char small[32];
    assert(formatStatusJson(small, sizeof(small), status) == 0 && small[0] == '\0');
  }

  std::puts("stream contract ok");
  return 0;
}
`;
compileAndRun('stream-contract', program, []);

const jsonInclude = [
  process.env.ARDUINOJSON_INCLUDE,
  path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src'),
  path.join(root, 'third_party/ArduinoJson/src'),
].filter(Boolean).find(candidate => fs.existsSync(path.join(candidate, 'ArduinoJson.h')));

if (!jsonInclude) {
  console.log('Local camera stream contract passed; command parser skipped (ArduinoJson headers not found).');
  process.exit(0);
}

const parserProgram = String.raw`
#include "src/video/local_camera/local_camera_request.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace local_camera_contract;
using local_camera_stream::CommandStatus;

static LocalCameraCommand parse(const std::string& text) {
  LocalCameraCommand command;
  parseCommand(text.data(), text.size(), &command);
  return command;
}

int main() {
  // The exact Bridge start/keepalive message.
  LocalCameraCommand c = parse(
      "{\"v\":1,\"action\":\"stream\",\"session\":\"0123456789abcdef0123456789abcdef\","
      "\"host\":\"192.168.1.20\",\"port\":8124,\"token\":\"fedcba9876543210fedcba9876543210\","
      "\"width\":640,\"height\":360,\"fps\":15,\"quality\":65,\"ttl_ms\":6000}");
  assert(c.kind == CommandKind::Stream && c.stream_status == CommandStatus::Ok);
  assert(strcmp(c.stream.session, "0123456789abcdef0123456789abcdef") == 0);
  assert(strcmp(c.stream.host, "192.168.1.20") == 0 && c.stream.port == 8124);
  assert(c.stream.ttl_ms == 6000 && c.stream.hints.width == 640 && c.stream.hints.fps == 15);
  // Unknown fields and deviating hints never reject.
  c = parse("{\"v\":1,\"action\":\"stream\",\"session\":\"0123456789abcdef\","
            "\"host\":\"10.0.0.2\",\"port\":8131,\"token\":\"fedcba9876543210\","
            "\"width\":\"wide\",\"fps\":500,\"mode\":\"720p5\",\"extra\":[1]}");
  assert(c.kind == CommandKind::Stream && c.stream_status == CommandStatus::Ok);
  assert(c.stream.hints.width == 640 && c.stream.hints.fps == 15 && c.stream.ttl_ms == 6000);
  // Invalid fields.
  assert(parse("{\"v\":1,\"action\":\"stream\",\"session\":\"0123456789abcdef\",\"host\":\"10.0.0.2\","
               "\"port\":\"8124\",\"token\":\"fedcba9876543210\"}").stream_status ==
         CommandStatus::InvalidEndpoint);
  assert(parse("{\"v\":1,\"action\":\"stream\",\"session\":\"0123456789abcdef\",\"host\":\"10.0.0.2\","
               "\"port\":8124,\"token\":\"fedcba9876543210\",\"ttl_ms\":\"6000\"}").stream_status ==
         CommandStatus::InvalidTtl);
  assert(parse("{\"v\":2,\"action\":\"stream\",\"session\":\"0123456789abcdef\",\"host\":\"10.0.0.2\","
               "\"port\":8124,\"token\":\"fedcba9876543210\"}").stream_status ==
         CommandStatus::UnsupportedVersion);
  c = parse("{\"v\":1,\"action\":\"record\"}");
  assert(c.kind == CommandKind::Stream && c.stream_status == CommandStatus::UnsupportedAction);
  c = parse("{\"v\":1,\"action\":7}");
  assert(c.kind == CommandKind::Stream && c.stream_status == CommandStatus::UnsupportedAction);
  // Stop.
  c = parse("{\"v\":1,\"action\":\"stream_stop\",\"session\":\"0123456789abcdef0123456789abcdef\"}");
  assert(c.kind == CommandKind::StreamStop && c.stream_status == CommandStatus::Ok);
  assert(strcmp(c.stop_session, "0123456789abcdef0123456789abcdef") == 0);
  assert(parse("{\"v\":1,\"action\":\"stream_stop\"}").stream_status == CommandStatus::InvalidSession);
  // Privacy pause and resume from Home Assistant.
  c = parse("{\"v\":1,\"action\":\"pause\"}");
  assert(c.kind == CommandKind::Pause && c.stream_status == CommandStatus::Ok);
  c = parse("{\"v\":1,\"action\":\"resume\"}");
  assert(c.kind == CommandKind::Resume && c.stream_status == CommandStatus::Ok);
  c = parse("{\"v\":2,\"action\":\"pause\"}");
  assert(c.kind == CommandKind::Pause && c.stream_status == CommandStatus::UnsupportedVersion);
  // Snapshot requests keep their shape and status codes.
  c = parse("{\"v\":1,\"id\":\"00112233445566778899aabbccddeeff\",\"op\":\"snapshot\",\"max_bytes\":65536}");
  assert(c.kind == CommandKind::Snapshot && c.snapshot_status == RequestStatus::Ok);
  assert(c.snapshot.max_bytes == 65536);
  assert(parse("not json").snapshot_status == RequestStatus::Malformed);
  assert(parse("").snapshot_status == RequestStatus::Empty);
  assert(parse(std::string(600, ' ') + "{}").snapshot_status == RequestStatus::TooLong);
  assert(parse("{\"v\":1,\"id\":\"0123456789abcdef\",\"op\":\"stream\"}").snapshot_status ==
         RequestStatus::UnsupportedOperation);
  std::puts("command parser ok");
  return 0;
}
`;
compileAndRun('stream-parser', parserProgram, [jsonInclude]);
console.log('Local camera stream contract and command parser passed.');
