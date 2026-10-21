// Host regression for clips the Bridge decodes for the speaker
// (src/audio/audio_stream_contract.h, Bridge audio_stream.py).
//
// b501 downloaded and decoded every clip on the panel. While a song played,
// the download filled the TCP window and 64 KB waited in internal RAM for the
// whole song (168 -> 80 KB free on a V2): the UI and the link slowed down.
// The Bridge now sends PCM over the camera's acknowledged transport. This
// test checks the stream URL, the format message and the incremental reader
// the speaker worker uses without waiting on the socket: messages split at
// every possible point, an acknowledgement per format and audio message, and
// the malformed streams it must refuse. The worker side (speaker.cpp) is
// checked for reading only while its PSRAM buffer has room.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const out = path.join(root, 'build/tests/audio-stream-contract');
fs.mkdirSync(out, {recursive: true});

// The worker: the Bridge stream reads only into free buffer space, sends one
// acknowledgement per message and never retries a used token.
const speaker = fs.readFileSync(path.join(root, 'src/audio/speaker.cpp'), 'utf8');
const pumpBridge = speaker.slice(speaker.indexOf('void pumpBridge('), speaker.indexOf('void pump(Source*'));
assert.ok(pumpBridge.length > 0, 'speaker.cpp has the Bridge stream pump');
assert.match(pumpBridge, /want = src->buffer\.writable\(\);\s*if \(want == 0\) \{/,
             'audio is read only while the PSRAM buffer has room');
assert.match(pumpBridge, /recv\(src->fd, destination, want, MSG_DONTWAIT\)/,
             'the pump never waits on the socket (the I2S output must stay fed)');
assert.match(pumpBridge, /rx\.takeAck\(&sequence, &length\)[\s\S]*tcp_ack::encodeAck/,
             'every complete message is acknowledged');
assert.match(speaker, /const bool bridge_stream = strncmp\(request\.url, "tcp:\/\/", 6\) == 0;/);
assert.match(speaker, /g_clip_paused && !stopRequested\(generation\) &&\s*!bridge_stream\) \{/,
             'a paused Bridge stream is not retried with its used token');
assert.match(speaker, /fields\.bridge_stream = kSupported;/,
             'the status announces Bridge streams');
// b502 seek: the new stream reported "playing" from 0 without a length before
// its format arrived, and Home Assistant's player bar vanished or jumped back.
assert.match(speaker, /if \(!bridge_stream\) setPublic\(PublicState::Playing, ErrorCode::None\);/,
             'a Bridge stream does not report "playing" before its format');
assert.match(speaker, /g_duration_ms\.store\(format\.duration_ms\);\s*setStatusId\(id\);\s*setPublic\(PublicState::Playing, ErrorCode::None\);/,
             'the format reports the clip with its position and length');

function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: audio stream contract test needs a host C++ compiler');
  process.exit(0);
}

const program = String.raw`
#include "src/audio/audio_stream_contract.h"
#include "src/audio/speaker_contract.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace audio_stream;

static bool url(const char* text, Endpoint* out = nullptr) {
  Endpoint scratch;
  return parseStreamUrl(text, strlen(text), out ? out : &scratch);
}

static void header(std::vector<uint8_t>& v, uint8_t type, uint32_t sequence, uint32_t length) {
  uint8_t h[tcp_ack::kFrameHeaderBytes];
  tcp_ack::encodeFrameHeader(h, type, sequence, length);
  v.insert(v.end(), h, h + sizeof(h));
}

static void format(std::vector<uint8_t>& v, uint32_t sequence, uint32_t rate, uint16_t channels,
                   uint16_t bits, uint32_t position, uint32_t duration) {
  header(v, kMessageFormat, sequence, kFormatBytes);
  uint8_t f[kFormatBytes];
  tcp_ack::writeBe32(f, rate);
  tcp_ack::writeBe16(f + 4, channels);
  tcp_ack::writeBe16(f + 6, bits);
  tcp_ack::writeBe32(f + 8, position);
  tcp_ack::writeBe32(f + 12, duration);
  v.insert(v.end(), f, f + sizeof(f));
}

static void audio(std::vector<uint8_t>& v, uint32_t sequence, uint32_t length, uint8_t seed) {
  header(v, kMessageAudio, sequence, length);
  for (uint32_t i = 0; i < length; ++i) v.push_back(static_cast<uint8_t>(seed + i));
}

struct Result {
  Receiver::Need need;
  std::vector<uint8_t> pcm;
  std::vector<std::pair<uint32_t, uint32_t>> acks;
  Format format;
  std::string failure;
};

// Feeds the stream in reads of at most "step" bytes (the socket may return
// any amount), the way the speaker's pump does, taking each acknowledgement
// before reading on.
static Result run(const std::vector<uint8_t>& stream, size_t step) {
  Receiver rx;
  Result r;
  size_t pos = 0;
  for (;;) {
    uint32_t sequence = 0, length = 0;
    if (rx.takeAck(&sequence, &length)) r.acks.push_back({sequence, length});
    const Receiver::Need need = rx.need();
    if (need == Receiver::Need::Ended || need == Receiver::Need::Failed) break;
    if (pos >= stream.size()) break;
    size_t want = need == Receiver::Need::Audio ? rx.audioMissing() : rx.scratchMissing();
    if (want > step) want = step;
    if (want > stream.size() - pos) want = stream.size() - pos;
    if (need == Receiver::Need::Audio) {
      r.pcm.insert(r.pcm.end(), stream.begin() + pos, stream.begin() + pos + want);
      rx.audioReceived(want);
    } else {
      memcpy(rx.scratch(), stream.data() + pos, want);
      rx.scratchReceived(want);
    }
    pos += want;
  }
  r.need = rx.need();
  r.format = rx.format();
  r.failure = rx.failure();
  assert(rx.formatReady() == (r.format.rate != 0));
  return r;
}

int main() {
  // Stream URLs as the Bridge builds them (camera_stream.stream_url).
  Endpoint e;
  assert(url("tcp://192.168.1.2:8124/AbC-_09xyz", &e));
  assert(strcmp(e.host, "192.168.1.2") == 0 && e.port == 8124 && strcmp(e.token, "AbC-_09xyz") == 0);
  assert(url("tcp://[fd00::1:2]:8125/tok", &e) && strcmp(e.host, "fd00::1:2") == 0 && e.port == 8125);
  assert(url("tcp://homeassistant.local:8131/t", &e) && strcmp(e.host, "homeassistant.local") == 0);
  for (const char* bad : {"tcp://", "tcp://:8124/t", "tcp://h/t", "tcp://h:0/t", "tcp://h:65536/t",
                          "tcp://h:8124/", "tcp://h:8124", "tcp://h:8124/a/b", "tcp://h:8124/a?b",
                          "tcp://h_x:8124/t", "tcp://[fd00::1:8124/t", "http://h:8124/t",
                          "tcp://h:123456/t"}) {
    assert(!url(bad));
  }
  const std::string long_token = "tcp://h:1/" + std::string(kMaxTokenLength + 1, 'a');
  assert(!url(long_token.c_str()));
  const std::string long_host = "tcp://" + std::string(kMaxHostLength + 1, 'a') + ":1/t";
  assert(!url(long_host.c_str()));

  // The speaker contract takes them as play URLs and announces the transport.
  using speaker_contract::ErrorCode;
  auto play_url = [](const char* u) { return speaker_contract::validateUrl(u, strlen(u), false); };
  assert(play_url("tcp://192.168.1.2:8124/AbC-_09xyz") == ErrorCode::None);
  assert(play_url("tcp://192.168.1.2/AbC") == ErrorCode::InvalidUrl);
  speaker_contract::StatusFields fields;
  char status[320];
  assert(speaker_contract::buildStatusJson(status, sizeof(status), fields) > 0);
  assert(strstr(status, "\"stream\"") == nullptr);
  fields.bridge_stream = true;
  assert(speaker_contract::buildStatusJson(status, sizeof(status), fields) > 0);
  assert(strstr(status, ",\"pausable\":true,\"stream\":\"tcp-ack-v1\"") != nullptr);
  // The longest status still fits the firmware's 320-byte buffer.
  fields.state = speaker_contract::PublicState::Playing;
  fields.id = "0123456789abcdef0123456789abcdef";
  fields.error = ErrorCode::UnsupportedFormat;
  fields.position_ms = 4294967295u;
  fields.duration_ms = 4294967295u;
  assert(speaker_contract::buildStatusJson(status, sizeof(status), fields) > 0);

  // A whole clip, split at every read size: same PCM, one ack per message.
  std::vector<uint8_t> clip;
  format(clip, 1, 22050, 1, 16, 90500, 162120);
  audio(clip, 2, 8192, 7);
  audio(clip, 3, 8192, 99);
  audio(clip, 4, 1234, 3);
  header(clip, kMessageEnd, 5, 0);
  std::vector<uint8_t> expected;
  for (uint32_t i = 0; i < 8192; ++i) expected.push_back(static_cast<uint8_t>(7 + i));
  for (uint32_t i = 0; i < 8192; ++i) expected.push_back(static_cast<uint8_t>(99 + i));
  for (uint32_t i = 0; i < 1234; ++i) expected.push_back(static_cast<uint8_t>(3 + i));
  for (size_t step : {size_t(1), size_t(2), size_t(3), size_t(7), size_t(15), size_t(16),
                      size_t(17), size_t(1000), size_t(4096), size_t(100000)}) {
    const Result r = run(clip, step);
    assert(r.need == Receiver::Need::Ended);
    assert(r.pcm == expected);
    assert(r.format.rate == 22050 && r.format.position_ms == 90500 && r.format.duration_ms == 162120);
    assert((r.acks == std::vector<std::pair<uint32_t, uint32_t>>{
        {1, 16}, {2, 8192}, {3, 8192}, {4, 1234}}));
  }

  // Refused: audio before the format, a second format, odd or oversized
  // audio, a stereo or 8-bit format, a foreign magic, an end with payload.
  auto fails = [](const std::vector<uint8_t>& v, const char* reason) {
    const Result r = run(v, 5);
    assert(r.need == Receiver::Need::Failed);
    assert(r.failure == reason);
  };
  std::vector<uint8_t> v;
  audio(v, 1, 16, 0);
  fails(v, "bad_audio");
  v.clear(); format(v, 1, 44100, 1, 16, 0, 0); format(v, 2, 44100, 1, 16, 0, 0);
  fails(v, "bad_format");
  v.clear(); format(v, 1, 44100, 1, 16, 0, 0); audio(v, 2, 15, 0);
  fails(v, "bad_audio");
  v.clear(); format(v, 1, 44100, 1, 16, 0, 0); header(v, kMessageAudio, 2, kMaxAudioBytes + 2);
  fails(v, "bad_audio");
  v.clear(); format(v, 1, 44100, 2, 16, 0, 0);
  fails(v, "unsupported_format");
  v.clear(); format(v, 1, 44100, 1, 8, 0, 0);
  fails(v, "unsupported_format");
  v.clear(); format(v, 1, 96000, 1, 16, 0, 0);
  fails(v, "unsupported_format");
  v.clear(); header(v, kMessageEnd, 1, 4);
  fails(v, "bad_end");
  v.clear(); header(v, kMessageFormat, 1, 15);
  fails(v, "bad_format");
  v.clear(); header(v, 9, 1, 0);
  fails(v, "bad_audio");
  v.assign(16, 0);
  fails(v, "bad_magic");
  // An empty clip (FFmpeg wrote nothing) ends cleanly after its format.
  v.clear(); format(v, 1, 44100, 1, 16, 0, 0); header(v, kMessageEnd, 2, 0);
  assert(run(v, 3).need == Receiver::Need::Ended);

  // The PSRAM buffer in steady state (b502): full, the output takes 2 KB, the
  // network refills what fits. b502 moved the whole unread tail (254 KB) for
  // every 2 KB played; at most about one byte per byte played may move.
  {
    static uint8_t memory[speaker_contract::kStreamBufferBytes];
    speaker_contract::StreamBuffer buffer;
    buffer.reset(memory, sizeof(memory));
    buffer.commit(buffer.writable());
    uint64_t moved = 0, played = 0;
    for (int i = 0; i < 2000; ++i) {
      assert(buffer.buffered() >= 2048);
      buffer.consume(2048);
      played += 2048;
      const size_t start = buffer.start, end = buffer.end;
      const size_t room = buffer.writable();
      if (start != 0 && buffer.start == 0) moved += end - start;
      buffer.commit(room);
      // The output never runs dry: at least half the buffer stays queued.
      assert(buffer.buffered() >= sizeof(memory) / 2 - 2048);
    }
    assert(moved <= played + played / 10);
  }
  std::puts("audio stream contract ok");
  return 0;
}
`;

const cpp = path.join(out, 'audio_stream_contract.cpp');
const exe = path.join(out, process.platform === 'win32' ? 'audio_stream_contract.exe' : 'audio_stream_contract');
fs.writeFileSync(cpp, program);
const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root, cpp, '-o', exe],
                        {encoding: 'utf8'});
assert.equal(build.status, 0, `audio stream contract did not compile:\n${build.stdout}${build.stderr}`);
const run = spawnSync(exe, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `audio stream contract failed:\n${run.stdout}${run.stderr}`);
console.log(run.stdout.trim());
