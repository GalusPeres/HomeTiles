// Host regression for the speaker MQTT contract shared with the Home
// Assistant Bridge media_player: retained status payloads, URL validation,
// volume mapping, clip format detection, the streamed WAV header parser and
// the stream buffer used by the playback worker. A second program exercises
// the ArduinoJson command parser when the pinned library is installed.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const out = path.join(root, 'build/tests/speaker-contract');
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
  console.log('SKIP: speaker contract test needs a host C++ compiler');
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

const contractProgram = String.raw`
#include "src/audio/speaker_contract.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace speaker_contract;

static std::string status(const StatusFields& fields) {
  char buffer[320];
  const size_t length = buildStatusJson(buffer, sizeof(buffer), fields);
  assert(length == strlen(buffer));
  return buffer;
}

static void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back(x >> 8);
}
static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF);
}
static void tag(std::vector<uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }

static std::vector<uint8_t> wav(uint16_t format, uint16_t channels, uint32_t rate,
                                uint16_t bits, uint32_t data_bytes, bool extra_chunk) {
  std::vector<uint8_t> v;
  tag(v, "RIFF");
  put32(v, 0);
  tag(v, "WAVE");
  if (extra_chunk) {
    // Odd-sized chunk with its pad byte before fmt.
    tag(v, "LIST");
    put32(v, 3);
    v.push_back('a'); v.push_back('b'); v.push_back('c'); v.push_back(0);
  }
  tag(v, "fmt ");
  put32(v, 16);
  put16(v, format);
  put16(v, channels);
  put32(v, rate);
  put32(v, rate * channels * bits / 8);
  put16(v, channels * bits / 8);
  put16(v, bits);
  tag(v, "data");
  put32(v, data_bytes);
  for (uint32_t i = 0; i < data_bytes; ++i) v.push_back(static_cast<uint8_t>(i));
  return v;
}

int main() {
  // Retained status: idle by default, optional id and error.
  StatusFields fields;
  assert(status(fields) ==
         "{\"v\":1,\"state\":\"idle\",\"volume\":0.50,\"muted\":false,"
         "\"formats\":[\"mp3\",\"wav\"],\"max_bytes\":4194304,\"pausable\":true}");
  fields.state = PublicState::Playing;
  fields.volume = 0.8f;
  fields.muted = true;
  fields.id = "0123456789abcdef";
  assert(status(fields) ==
         "{\"v\":1,\"state\":\"playing\",\"volume\":0.80,\"muted\":true,"
         "\"formats\":[\"mp3\",\"wav\"],\"max_bytes\":4194304,\"pausable\":true,"
         "\"id\":\"0123456789abcdef\",\"position_ms\":0}");
  // Position while a clip plays or pauses, the length once it is known.
  fields.state = PublicState::Paused;
  fields.position_ms = 61500;
  fields.duration_ms = 213000;
  assert(status(fields).find("\"state\":\"paused\"") != std::string::npos);
  assert(status(fields).find(",\"position_ms\":61500,\"duration_ms\":213000}") != std::string::npos);
  // Paused with the stream closed (b512): the Bridge plays it again to resume.
  assert(status(fields).find("released") == std::string::npos);
  fields.released = true;
  assert(status(fields).find(",\"duration_ms\":213000,\"released\":true}") != std::string::npos);
  fields.state = PublicState::Playing;
  assert(status(fields).find("released") == std::string::npos);
  fields.released = false;
  // The longest status fits the firmware's 320-byte buffer.
  fields.state = PublicState::Playing;
  fields.id = "0123456789abcdef0123456789abcdef";
  fields.error = ErrorCode::UnsupportedFormat;
  fields.position_ms = 4294967295u;
  fields.duration_ms = 4294967295u;
  char longest[320];
  assert(buildStatusJson(longest, sizeof(longest), fields) > 0);
  fields.state = PublicState::Paused;
  fields.released = true;
  assert(buildStatusJson(longest, sizeof(longest), fields) > 0);
  fields.state = PublicState::Playing;
  fields.released = false;
  fields.id = "0123456789abcdef";
  fields.error = ErrorCode::None;
  fields.position_ms = 0;
  fields.duration_ms = 0;
  fields.state = PublicState::Idle;
  fields.error = ErrorCode::HttpsUnsupported;
  assert(status(fields).find(",\"error\":\"https_unsupported\"}") != std::string::npos);
  // Idle sends no position.
  assert(status(fields).find("position_ms") == std::string::npos);

  // Clip length: Xing/Info or VBRI frame count, else size and bitrate.
  {
    uint8_t frame[256] = {0xFF, 0xFB, 0x90, 0x44};  // MPEG-1 L3 44.1 kHz joint stereo
    memcpy(frame + 36, "Info", 4);
    frame[36 + 7] = 0x01;                       // frames field present
    frame[36 + 8] = 0x00; frame[36 + 9] = 0x00; frame[36 + 10] = 0x01; frame[36 + 11] = 0x00;
    assert(mp3TagFrameCount(frame, sizeof(frame)) == 256);
    assert(mp3DurationMs(frame, sizeof(frame), 44100, 128000, 1000000) == 256u * 1152u * 1000u / 44100u);
    uint8_t mono25[64] = {0xFF, 0xE3, 0x18, 0xC4};  // MPEG-2.5 8 kHz mono
    memcpy(mono25 + 13, "Xing", 4);
    mono25[13 + 7] = 0x01;
    mono25[13 + 11] = 11;
    assert(mp3TagFrameCount(mono25, sizeof(mono25)) == 11);
    assert(mp3DurationMs(mono25, sizeof(mono25), 8000, 8000, 792) == 11u * 576u * 1000u / 8000u);
    uint8_t vbri[64] = {0xFF, 0xFB, 0x90, 0x44};
    memcpy(vbri + 36, "VBRI", 4);
    vbri[36 + 17] = 100;
    assert(mp3TagFrameCount(vbri, sizeof(vbri)) == 100);
    // No tag: constant bitrate from the size; a live stream stays unknown.
    uint8_t plain[64] = {0xFF, 0xFB, 0x90, 0x44};
    assert(mp3TagFrameCount(plain, sizeof(plain)) == 0);
    assert(mp3DurationMs(plain, sizeof(plain), 44100, 128000, 3200000) == 200000);
    assert(mp3DurationMs(plain, sizeof(plain), 44100, 128000, -1) == 0);
    assert(mp3DurationMs(plain, sizeof(plain), 0, 128000, 3200000) == 0);
    assert(mp3TagFrameCount(frame, 20) == 0);  // too short for the tag
    assert(wavDurationMs(176400, 44100, 4) == 1000);
    assert(wavDurationMs(0xFFFFFFFFu, 44100, 4) == 0);
    assert(wavDurationMs(0, 44100, 4) == 0);
  }
  // Invalid ids never reach the payload.
  fields.id = "not-an-id";
  assert(status(fields).find("\"id\"") == std::string::npos);
  // Too small a buffer fails instead of truncating.
  char tiny[32];
  assert(buildStatusJson(tiny, sizeof(tiny), fields) == 0);

  // Every error has a stable, non-empty name except None.
  for (ErrorCode code : {ErrorCode::InvalidUrl, ErrorCode::HttpsUnsupported,
                         ErrorCode::DownloadFailed, ErrorCode::TooLarge,
                         ErrorCode::UnsupportedFormat, ErrorCode::DecodeFailed,
                         ErrorCode::CodecUnavailable, ErrorCode::OutOfMemory}) {
    assert(strlen(errorCodeName(code)) > 0);
  }
  assert(strcmp(errorCodeName(ErrorCode::None), "") == 0);

  // Request ids: 16-32 lowercase hex.
  assert(isValidRequestId("0123456789abcdef", 16));
  assert(!isValidRequestId("0123456789abcde", 15));
  assert(!isValidRequestId("0123456789ABCDEF", 16));
  assert(!isValidRequestId("0123456789abcdef0123456789abcdef0", 33));

  // URLs: absolute http with a host; https only where allowed.
  auto url = [](const char* u, bool https) { return validateUrl(u, strlen(u), https); };
  assert(url("http://192.168.1.10:8123/api/tts_proxy/abc.mp3", false) == ErrorCode::None);
  assert(url("https://ha.example/api/tts_proxy/abc.mp3", false) == ErrorCode::HttpsUnsupported);
  assert(url("https://ha.example/api/tts_proxy/abc.mp3", true) == ErrorCode::None);
  assert(url("http:///nohost", false) == ErrorCode::InvalidUrl);
  assert(url("http://", false) == ErrorCode::InvalidUrl);
  assert(url("ftp://host/file.mp3", false) == ErrorCode::InvalidUrl);
  assert(url("/api/tts_proxy/abc.mp3", false) == ErrorCode::InvalidUrl);
  assert(url("http://host/a b.mp3", false) == ErrorCode::InvalidUrl);
  assert(url("http://host/\x7f", false) == ErrorCode::InvalidUrl);
  assert(validateUrl(nullptr, 0, true) == ErrorCode::InvalidUrl);
  std::string long_url = "http://h/" + std::string(kMaxUrlLength, 'a');
  assert(url(long_url.c_str(), false) == ErrorCode::InvalidUrl);

  // Volume: clamped, 1.0 = 0 dB (0xBF), 0 = mute, monotonic in between.
  assert(clampVolume(-1.0) == 0.0f);
  assert(clampVolume(2.0) == 1.0f);
  assert(clampVolume(0.0 / 0.0) == kDefaultVolume);
  assert(volumeRegister(1.0f) == 0xBF);
  assert(volumeRegister(0.0f) == 0);
  assert(volumeRegister(0.5f) == 0xBF - 50);
  assert(volumeRegister(0.001f) >= 1);
  uint8_t previous = 0;
  for (int i = 1; i <= 100; ++i) {
    const uint8_t reg = volumeRegister(i / 100.0f);
    assert(reg >= previous);
    assert(reg <= 0xBF);
    previous = reg;
  }
  // A board's limit: 100 % plays like the limit's share of the shared scale,
  // every step below scales with it; an invalid limit is the full scale.
  assert(limitedVolumeRegister(1.0f, 1.0f) == volumeRegister(1.0f));
  assert(limitedVolumeRegister(1.0f, 0.6f) == volumeRegister(0.6f));
  assert(limitedVolumeRegister(0.5f, 0.6f) == volumeRegister(0.3f));
  assert(limitedVolumeRegister(1.0f, 0.9f) == volumeRegister(0.9f));
  assert(limitedVolumeRegister(0.0f, 0.6f) == 0);
  assert(limitedVolumeRegister(2.0f, 0.6f) == volumeRegister(0.6f));
  assert(limitedVolumeRegister(1.0f, 0.0f) == volumeRegister(1.0f));
  assert(limitedVolumeRegister(1.0f, 1.5f) == volumeRegister(1.0f));
  assert(limitedVolumeRegister(1.0f, 0.6f) < 0xBF);

  // Format detection by content.
  const uint8_t id3[] = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0};
  const uint8_t sync[] = {0xFF, 0xFB, 0x90, 0x00};
  const uint8_t layer2[] = {0xFF, 0xFD, 0x90, 0x00};
  const uint8_t html[] = {'<', 'h', 't', 'm', 'l', '>'};
  assert(detectFormat(id3, sizeof(id3)) == ClipFormat::Mp3);
  assert(detectFormat(sync, sizeof(sync)) == ClipFormat::Mp3);
  assert(detectFormat(layer2, sizeof(layer2)) == ClipFormat::Unknown);
  assert(detectFormat(html, sizeof(html)) == ClipFormat::Unknown);
  assert(detectFormat(nullptr, 0) == ClipFormat::Unknown);

  // WAV header: 16-bit PCM mono and stereo, padded chunks, streamed headers.
  WavInfo info;
  auto header = [&](const std::vector<uint8_t>& v, size_t n) {
    return parseWavHeader(v.data(), n, &info);
  };
  std::vector<uint8_t> mono = wav(1, 1, 22050, 16, 100, false);
  assert(detectFormat(mono.data(), mono.size()) == ClipFormat::Wav);
  assert(header(mono, mono.size()) == WavHeaderResult::Ok);
  assert(info.channels == 1 && info.sample_rate == 22050);
  assert(info.data_offset == 44 && info.data_length == 100);
  // Every prefix shorter than the header asks for more bytes.
  for (size_t n = 0; n < 44; ++n) assert(header(mono, n) == WavHeaderResult::Incomplete);
  assert(header(mono, 44) == WavHeaderResult::Ok);
  std::vector<uint8_t> stereo = wav(1, 2, 44100, 16, 102, true);
  assert(header(stereo, stereo.size()) == WavHeaderResult::Ok);
  assert(info.channels == 2 && info.data_offset == 56 && info.data_length == 102);
  // Live WAV streams announce an unknown length.
  std::vector<uint8_t> live = wav(1, 1, 16000, 16, 0, false);
  live[40] = live[41] = live[42] = live[43] = 0xFF;
  assert(header(live, live.size()) == WavHeaderResult::Ok && info.data_length == 0xFFFFFFFFu);
  std::vector<uint8_t> eight_bit = wav(1, 1, 8000, 8, 100, false);
  assert(header(eight_bit, eight_bit.size()) == WavHeaderResult::Invalid);
  std::vector<uint8_t> floats = wav(3, 1, 48000, 16, 100, false);
  assert(header(floats, floats.size()) == WavHeaderResult::Invalid);
  std::vector<uint8_t> surround = wav(1, 6, 48000, 16, 120, false);
  assert(header(surround, surround.size()) == WavHeaderResult::Invalid);
  assert(parseWavHeader(id3, sizeof(id3), &info) == WavHeaderResult::Invalid);
  // A metadata chunk claiming megabytes is a broken header, not a wait.
  std::vector<uint8_t> huge = wav(1, 1, 16000, 16, 10, true);
  huge[16] = 0; huge[17] = 0; huge[18] = 0x10; huge[19] = 0;
  assert(header(huge, huge.size()) == WavHeaderResult::Invalid);

  // Content types: generic or MP3 types pass, other codecs and pages do not.
  assert(!isUnsupportedContentType(nullptr));
  assert(!isUnsupportedContentType(""));
  assert(!isUnsupportedContentType("audio/mpeg"));
  assert(!isUnsupportedContentType("audio/MPEG; charset=binary"));
  assert(!isUnsupportedContentType("audio/wav"));
  assert(!isUnsupportedContentType("application/octet-stream"));
  assert(isUnsupportedContentType("audio/aacp"));
  assert(isUnsupportedContentType("audio/AAC"));
  assert(isUnsupportedContentType("application/ogg"));
  assert(isUnsupportedContentType("audio/flac"));
  assert(isUnsupportedContentType("audio/x-mpegurl"));
  assert(isUnsupportedContentType("text/html; charset=utf-8"));

  // ID3v2 tag length: syncsafe size plus header and optional footer.
  const uint8_t tag_plain[] = {'I', 'D', '3', 4, 0, 0x00, 0, 0, 0x02, 0x01};
  const uint8_t tag_footer[] = {'I', 'D', '3', 4, 0, 0x10, 0, 0, 0x02, 0x01};
  assert(id3v2Length(tag_plain, sizeof(tag_plain)) == 10 + 257);
  assert(id3v2Length(tag_footer, sizeof(tag_footer)) == 20 + 257);
  assert(id3v2Length(sync, sizeof(sync)) == 0);
  assert(id3v2Length(tag_plain, 9) == 0);

  // Prebuffer: enough bytes, the end of the download, or a slow stream's turn.
  assert(prebufferDone(kPrebufferBytes, 0, false));
  assert(!prebufferDone(kPrebufferBytes - 1, 0, false));
  assert(prebufferDone(0, 0, true));
  assert(!prebufferDone(kMinDecodeBytes, kPrebufferMaxWaitMs - 1, false));
  assert(prebufferDone(kMinDecodeBytes, kPrebufferMaxWaitMs, false));
  assert(!prebufferDone(kMinDecodeBytes - 1, 60000, false));

  // Stream buffer: reads stay contiguous and compaction keeps unread bytes.
  {
    uint8_t memory[16];
    StreamBuffer buffer;
    buffer.reset(memory, sizeof(memory));
    assert(buffer.writable() == 16 && buffer.buffered() == 0);
    for (int i = 0; i < 12; ++i) buffer.write()[i] = static_cast<uint8_t>(i);
    buffer.commit(12);
    buffer.consume(9);
    assert(buffer.buffered() == 3 && buffer.read()[0] == 9);
    // Read position past half: the tail moves to the front.
    assert(buffer.writable() == 13);
    assert(buffer.start == 0 && buffer.read()[0] == 9 && buffer.read()[2] == 11);
    buffer.commit(100);
    assert(buffer.buffered() == 16 && buffer.writable() == 0);
    buffer.consume(1);
    // Full buffer read less than half: it waits for the reader instead of
    // moving the whole tail for every byte read (b502 moved 254 KB per 2 KB).
    assert(buffer.writable() == 0 && buffer.read()[0] == 10);
    buffer.consume(7);
    // Half read: the tail moves to the front once.
    assert(buffer.writable() == 8 && buffer.start == 0 && buffer.buffered() == 8);
    buffer.consume(1000);
    assert(buffer.buffered() == 0 && buffer.start == 0 && buffer.writable() == 16);
  }

  // Stereo downmix averages each frame in place.
  int16_t samples[] = {1000, 3000, -32768, -32768, 32767, -32768};
  downmixToMono(samples, 3);
  assert(samples[0] == 2000 && samples[1] == -32768 && samples[2] == 0);

  puts("speaker contract ok");
  return 0;
}
`;

console.log(compileAndRun('speaker_contract', contractProgram, []).trim());

// ArduinoJson parser (speaker_request.h) when the pinned library is installed.
const arduinoJsonCandidates = [
  process.env.ARDUINOJSON_SRC,
  path.join(os.homedir(), 'Arduino/libraries/ArduinoJson/src'),
  path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src'),
].filter(Boolean);
const arduinoJson = arduinoJsonCandidates.find((dir) => fs.existsSync(path.join(dir, 'ArduinoJson.h')));
if (!arduinoJson) {
  console.log('SKIP: speaker command parser test needs the ArduinoJson library');
  process.exit(0);
}

const requestProgram = String.raw`
#include "src/audio/speaker_request.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace speaker_contract;

static Command parse(const std::string& json, bool https_allowed = false,
                     std::string* url = nullptr) {
  Command command;
  char buffer[kMaxUrlLength + 1];
  parseCommand(json.c_str(), json.size(), https_allowed, &command, buffer);
  if (url) *url = buffer;
  return command;
}

int main() {
  std::string url;
  Command c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
                    "\"url\":\"http://ha.local:8123/api/tts_proxy/x.mp3\"}", false, &url);
  assert(c.kind == CommandKind::Play && c.status == CommandStatus::Ok);
  assert(strcmp(c.id, "0123456789abcdef") == 0);
  assert(url == "http://ha.local:8123/api/tts_proxy/x.mp3" && c.url_length == url.size());
  assert(!c.live);
  // A camera's sound from the Bridge (b504): kept short on the panel.
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
            "\"url\":\"tcp://192.168.1.2:8124/tok\",\"live\":true}", false, &url);
  assert(c.status == CommandStatus::Ok && c.live && url == "tcp://192.168.1.2:8124/tok");
  assert(c.lead_ms == 0);
  // Lip sync (b506): how much earlier the Bridge sent the sound; clamped,
  // and only for a camera's sound.
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
            "\"url\":\"tcp://192.168.1.2:8124/tok\",\"live\":true,\"lead_ms\":151}");
  assert(c.status == CommandStatus::Ok && c.lead_ms == 151);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
            "\"url\":\"tcp://192.168.1.2:8124/tok\",\"live\":true,\"lead_ms\":5000}");
  assert(c.lead_ms == 1000);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
            "\"url\":\"tcp://192.168.1.2:8124/tok\",\"lead_ms\":151}");
  assert(c.lead_ms == 0);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
            "\"url\":\"tcp://192.168.1.2:8124/tok\",\"live\":\"yes\"}");
  assert(c.status == CommandStatus::Ok && !c.live);
  // Resume by playing again (b512): only a Bridge stream that is no camera's
  // sound, and only when the Bridge says so.
  assert(!c.restart);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
            "\"url\":\"tcp://192.168.1.2:8124/tok\",\"restart\":true}");
  assert(c.status == CommandStatus::Ok && c.restart);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
            "\"url\":\"tcp://192.168.1.2:8124/tok\",\"live\":true,\"restart\":true}");
  assert(c.status == CommandStatus::Ok && !c.restart);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
            "\"url\":\"http://ha.local:8123/x.mp3\",\"restart\":true}");
  assert(c.status == CommandStatus::Ok && !c.restart);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\","
            "\"url\":\"tcp://192.168.1.2:8124/tok\",\"restart\":\"yes\"}");
  assert(c.status == CommandStatus::Ok && !c.restart);

  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\",\"url\":\"https://ha/x.mp3\"}");
  assert(c.kind == CommandKind::Play && c.status == CommandStatus::HttpsUnsupported);
  // The id survives a refused URL so the status can name the request.
  assert(strcmp(c.id, "0123456789abcdef") == 0);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\",\"url\":\"https://ha/x.mp3\"}", true);
  assert(c.status == CommandStatus::Ok);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\"}");
  assert(c.status == CommandStatus::InvalidUrl);
  c = parse("{\"v\":1,\"op\":\"play\",\"id\":\"XYZ\",\"url\":\"http://h/x\"}");
  assert(c.status == CommandStatus::InvalidId);

  assert(parse("{\"v\":1,\"op\":\"stop\"}").kind == CommandKind::Stop);
  c = parse("{\"v\":1,\"op\":\"pause\"}");
  assert(c.kind == CommandKind::Pause && c.status == CommandStatus::Ok);
  c = parse("{\"v\":1,\"op\":\"resume\"}");
  assert(c.kind == CommandKind::Resume && c.status == CommandStatus::Ok);
  c = parse("{\"v\":1,\"op\":\"volume\",\"volume\":0.25}");
  assert(c.kind == CommandKind::Volume && c.status == CommandStatus::Ok && c.volume == 0.25f);
  c = parse("{\"v\":1,\"op\":\"volume\",\"volume\":1}");
  assert(c.status == CommandStatus::Ok && c.volume == 1.0f);
  assert(parse("{\"v\":1,\"op\":\"volume\",\"volume\":1.5}").status == CommandStatus::InvalidValue);
  assert(parse("{\"v\":1,\"op\":\"volume\",\"volume\":\"x\"}").status == CommandStatus::InvalidValue);
  c = parse("{\"v\":1,\"op\":\"mute\",\"muted\":true}");
  assert(c.kind == CommandKind::Mute && c.status == CommandStatus::Ok && c.muted);
  assert(parse("{\"v\":1,\"op\":\"mute\",\"muted\":1}").status == CommandStatus::InvalidValue);

  assert(parse("{\"v\":2,\"op\":\"stop\"}").status == CommandStatus::UnsupportedVersion);
  assert(parse("{\"op\":\"stop\"}").status == CommandStatus::UnsupportedVersion);
  assert(parse("{\"v\":1,\"op\":\"say\"}").status == CommandStatus::UnsupportedOp);
  assert(parse("{\"v\":1}").status == CommandStatus::UnsupportedOp);
  assert(parse("not json").status == CommandStatus::InvalidJson);
  assert(parse("[1]").status == CommandStatus::InvalidJson);
  std::string big = "{\"v\":1,\"op\":\"stop\",\"pad\":\"" + std::string(kMaxRequestPayloadBytes, 'a') + "\"}";
  assert(parse(big).status == CommandStatus::TooLarge);

  puts("speaker command parser ok");
  return 0;
}
`;

console.log(compileAndRun('speaker_request', requestProgram, [arduinoJson]).trim());
